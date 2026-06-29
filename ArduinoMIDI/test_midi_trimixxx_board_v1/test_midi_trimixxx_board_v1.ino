/* ============================================================================
 * TriMixxx — Board Bring-Up / Wiring Test Harness
 * Target: ATmega32U4 Pro Micro (5V / 16MHz)
 * Topology: Pi --UART--> Arduino --I2C--> MCP23017 --JST--> button boards
 *
 * HOW TO USE
 *   1. Upload. Open Serial Monitor (USB) @ 115200 baud, line ending = Newline.
 *   2. Type a digit 1..6 + Enter to enter a test mode.
 *   3. Type 'm' to return to the menu, '?' to reprint it.
 *
 * Each menu item = one board/stage. Run them in order; the menu shows which
 * stages exist so it doubles as a progress checklist.
 *
 * ASSUMPTIONS (change the #defines if your wiring differs):
 *   - MCP23017 at I2C address 0x20 (A0/A1/A2 tied to GND). Test 1 reports the
 *     real address if it's different.
 *   - Buttons are wired to GND and use the MCP's internal pull-ups => pressed
 *     reads LOW (0). Loop board buttons additionally have a 100nF cap to GND
 *     for hardware debounce:  PIN --(100nF--GND)--SW--GND.
 *     The internal pull-up is REQUIRED here: it's what pulls the line HIGH on
 *     release and recharges the cap. With ~100k pull-up + 100nF the release
 *     edge rises with tau ~10ms, so the software debounce window is set wider
 *     than that (see DEBOUNCE_SAMPLES) to avoid double-triggering.
 *   - LED channels are active-HIGH: MCP pin HIGH -> 2N7000 gate HIGH -> LED on.
 *   - Jogwheel touch (TCH) is active-LOW with the 22k pull-up on the JOGB PCB
 *     (pressed = LOW). PE6 (D7) set as plain INPUT, no internal pull-up.
 *   - Jog encoder uses the PJRC Encoder lib; D8/D9 aren't INT pins so the lib
 *     polls inside read() - fine for hand speed, may slip at full scratch.
 *
 * PIN MAP (Pro Micro / ATmega32U4)
 *   Tempo fader  ADIN -> A0 (PF7)      ADCT -> A1 (PF6)
 *   Jogwheel     TCH  -> D7  (PE6)     JOG1 -> D8 (PB4)   JOG2 -> D9 (PB5)
 *   I2C          SDA  -> D2 (PD1)      SCL  -> D3 (PD0)
 *   UART to Pi   RX1  -> D0 (PD2)      TX1  -> D1 (PD3)   (Serial1)
 *
 * MCP23017 channel map
 *   Inputs  (PORTA): GPA0 LOOP_START_BTN  GPA1 LOOP_END_BTN
 *                    GPA4 BTN_PLAY        GPA5 BTN_CUE   (GPA6 = future reloop)
 *   Outputs (PORTB): GPB0 LOOP_START_LED  GPB1 LOOP_END_LED
 *                    GPB2 LED_PLAY        GPB3 LED_CUE
 * ==========================================================================*/

#include <Wire.h>
#include <Encoder.h>     // PJRC "Encoder" by Paul Stoffregen (same lib as the working jog sketch)

// ---------------------------------------------------------------- config ----
#define MCP_ADDR        0x20      // default when A0..A2 grounded
#define UART_BAUD       115200    // Pi link baud (match ttymidi config)
#define USB_BAUD        115200

// ---- Pro Micro pins
#define PIN_ADIN        A0        // PF7  tempo fader wiper (main channel)
#define PIN_ADCT        A1        // PF6  tempo calibration / control channel
#define PIN_TCH         7         // PE6  jogwheel touch
#define PIN_JOG1        8         // PB4  encoder A
#define PIN_JOG2        9         // PB5  encoder B

// ---- MCP23017 registers (IOCON.BANK = 0, the power-on default)
#define MCP_IODIRA      0x00
#define MCP_IODIRB      0x01
#define MCP_GPPUA       0x0C
#define MCP_GPPUB       0x0D
#define MCP_GPIOA       0x12
#define MCP_GPIOB       0x13
#define MCP_OLATB       0x15

// ---- MCP bit masks (PORTA inputs / PORTB outputs)
#define M_LOOP_START_BTN  (1 << 0)   // GPA0
#define M_LOOP_END_BTN    (1 << 1)   // GPA1
#define M_BTN_PLAY        (1 << 4)   // GPA4
#define M_BTN_CUE         (1 << 5)   // GPA5

#define M_LOOP_START_LED  (1 << 0)   // GPB0
#define M_LOOP_END_LED    (1 << 1)   // GPB1
#define M_LED_PLAY        (1 << 2)   // GPB2
#define M_LED_CUE         (1 << 3)   // GPB3

#define IODIRA_INPUTS   (M_LOOP_START_BTN | M_LOOP_END_BTN | M_BTN_PLAY | M_BTN_CUE)
#define IODIRB_OUTPUTS  (M_LOOP_START_LED | M_LOOP_END_LED | M_LED_PLAY | M_LED_CUE)

// ----------------------------------------------------------------- state ----
enum Mode { MENU, T1_I2C, T2_FADER, T3_JOG, T4_PLAYCUE, T5_LOOP, T6_UART };
Mode mode = MENU;
bool mcpReady = false;
uint8_t mcpFoundAddr = 0;

unsigned long lastTick = 0;

// jog encoder (PJRC Encoder lib handles decoding; polls in read() on D8/D9)
Encoder jogWheel(PIN_JOG1, PIN_JOG2);

// ============================================================ MCP helpers ===
bool mcpWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MCP_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

uint8_t mcpRead(uint8_t reg) {
  Wire.beginTransmission(MCP_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFF;
  Wire.requestFrom((int)MCP_ADDR, 1);
  return Wire.available() ? Wire.read() : 0xFF;
}

// Read that reports success separately, so a failed transaction is never
// mistaken for real data (a failed read used to look like "all released").
bool mcpReadReg(uint8_t reg, uint8_t &val) {
  Wire.beginTransmission(MCP_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)MCP_ADDR, 1) != 1) return false;
  val = Wire.read();
  return true;
}

// Configure ports for the button-board tests. Returns false if MCP not talking.
bool mcpConfigure() {
  bool ok = true;
  ok &= mcpWrite(MCP_IODIRA, IODIRA_INPUTS);   // 1 = input
  ok &= mcpWrite(MCP_GPPUA,  IODIRA_INPUTS);   // pull-ups on the input pins
  ok &= mcpWrite(MCP_IODIRB, (uint8_t)~IODIRB_OUTPUTS); // 0 = output on LED bits
  ok &= mcpWrite(MCP_OLATB,  0x00);            // all LEDs off
  mcpReady = ok;
  return ok;
}

void ledWrite(uint8_t mask, bool on) {
  uint8_t v = mcpRead(MCP_OLATB);
  if (on) v |= mask; else v &= ~mask;
  mcpWrite(MCP_OLATB, v);
}

// (jog decoding handled by the Encoder library — no manual ISR needed)

// ============================================================ menu ==========
void printMenu() {
  Serial.println();
  Serial.println(F("====== TriMixxx Board Test ======"));
  Serial.println(F(" 1) I2C  -> MCP23017 comms"));
  Serial.println(F(" 2) Tempo fader  (A0 ADIN / A1 ADCT)"));
  Serial.println(F(" 3) Jogwheel     (TCH/JOG1/JOG2)"));
  Serial.println(F(" 4) Play/Cue board   (MCP)"));
  Serial.println(F(" 5) Loop button board(MCP)"));
  Serial.println(F(" 6) UART  -> Pi"));
  Serial.println(F("---------------------------------"));
  Serial.println(F(" m) back to menu   ?) reprint"));
  Serial.print  (F("select> "));
}

void enterMode(Mode m) {
  mode = m;
  Serial.println();
  switch (m) {
    case T1_I2C:     runI2CTest();      break;   // one-shot, returns to menu
    case T2_FADER:   Serial.println(F("[Test 2] Move the fader. 'm' to stop.")); break;
    case T3_JOG:
      pinMode(PIN_TCH, INPUT);          // active-LOW; 22k pull-up lives on JOGB PCB
      jogWheel.write(0);                // zero the counter (lib already set D8/D9 pull-ups)
      Serial.println(F("[Test 3] Spin & touch the jog. 'm' to stop."));
      break;
    case T4_PLAYCUE:
      if (!mcpConfigure()) { Serial.println(F("MCP not responding - run Test 1 first.")); mode = MENU; printMenu(); return; }
      Serial.println(F("[Test 4] LED self-test, then press PLAY/CUE. 'm' to stop."));
      ledSelfTest(M_LED_PLAY, M_LED_CUE);
      break;
    case T5_LOOP:
      if (!mcpConfigure()) { Serial.println(F("MCP not responding - run Test 1 first.")); mode = MENU; printMenu(); return; }
      Serial.println(F("[Test 5] Railroad blink. Hold a button to kill its LED. 'm' to stop."));
      break;
    case T6_UART:
      Serial.print(F("[Test 6] Serial1 @ ")); Serial.print(UART_BAUD);
      Serial.println(F(" baud. TX heartbeat + echo RX. 'm' to stop."));
      Serial.println(F("Loopback: jumper D1(TX)->D0(RX) and watch for RX echo."));
      break;
    default: break;
  }
}

// ============================================================ tests =========
void runI2CTest() {
  Serial.println(F("[Test 1] Scanning I2C bus..."));
  uint8_t count = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("  found device @ 0x"));
      if (addr < 16) Serial.print('0');
      Serial.println(addr, HEX);
      count++;
      if (addr >= 0x20 && addr <= 0x27) mcpFoundAddr = addr;  // MCP23017 range
    }
  }
  if (count == 0) {
    Serial.println(F("  NONE. Check SDA/SCL, pull-ups, power, GND."));
    mode = MENU; printMenu(); return;
  }

  // Two-way verify against the configured MCP_ADDR.
  Serial.print(F("  Verifying R/W @ 0x")); Serial.println(MCP_ADDR, HEX);
  bool w = mcpWrite(MCP_IODIRA, 0xA5);          // write a test pattern
  uint8_t rb = mcpRead(MCP_IODIRA);             // read it back
  mcpWrite(MCP_IODIRA, 0xFF);                   // restore (all inputs)
  if (w && rb == 0xA5) {
    Serial.println(F("  OK  - wrote 0xA5 to IODIRA, read back 0xA5. MCP talks."));
    mcpReady = true;
  } else {
    Serial.print(F("  FAIL - wrote 0xA5, read 0x")); Serial.println(rb, HEX);
    if (mcpFoundAddr && mcpFoundAddr != MCP_ADDR) {
      Serial.print(F("  NOTE: MCP appears at 0x")); Serial.print(mcpFoundAddr, HEX);
      Serial.println(F(" - update MCP_ADDR."));
    }
  }
  mode = MENU; printMenu();
}

void ledSelfTest(uint8_t a, uint8_t b) {
  for (uint8_t i = 0; i < 2; i++) {
    ledWrite(a, true);  delay(180);
    ledWrite(a, false); ledWrite(b, true); delay(180);
    ledWrite(b, false); delay(180);
  }
}

void loopFader() {
  if (millis() - lastTick < 150) return;
  lastTick = millis();
  Serial.print(F("ADIN(A0)=")); Serial.print(analogRead(PIN_ADIN));
  Serial.print(F("\tADCT(A1)=")); Serial.println(analogRead(PIN_ADCT));
}

void loopJog() {
  static bool first = true;
  static long shown = 0;
  static int lastTouch = -1;
  long pos = jogWheel.read();             // polls the encoder on D8/D9
  int touched = !digitalRead(PIN_TCH);    // active-LOW: pressed = 1
  if (first || pos != shown || touched != lastTouch) {
    first = false;
    shown = pos; lastTouch = touched;
    Serial.print(F("JOG pos=")); Serial.print(pos);
    Serial.print(F("\tTCH=")); Serial.println(touched ? F("TOUCH") : F("--"));
  }
}

// Echo buttons -> LEDs so a press visibly lights its channel (proves in+out+FET).
// Sampled at a fixed rate with a debounce integrator: a new state must hold for
// DEBOUNCE_SAMPLES consecutive reads before it's accepted, which kills the
// momentary "up" glitches while a button is held. Failed I2C reads are ignored.
#define DEBOUNCE_SAMPLES 6      // 6 x 4ms = 24ms stable; clears the ~10ms RC rise
#define SAMPLE_INTERVAL  4      // ms between reads

void loopButtons(uint8_t btnA, uint8_t ledA, const __FlashStringHelper* nameA,
                 uint8_t btnB, uint8_t ledB, const __FlashStringHelper* nameB) {
  static unsigned long lastSample = 0;
  static bool aState = false, bState = false;   // debounced/accepted state
  static uint8_t aCnt = 0, bCnt = 0;
  static bool first = true, lastA = false, lastB = false;

  if (millis() - lastSample < SAMPLE_INTERVAL) return;
  lastSample = millis();

  uint8_t gpioa;
  if (!mcpReadReg(MCP_GPIOA, gpioa)) return;     // bad read -> ignore, keep last state

  bool aRaw = !(gpioa & btnA);                   // active-low
  bool bRaw = !(gpioa & btnB);

  // integrate: only flip the accepted state after N consistent samples
  if (aRaw != aState) { if (++aCnt >= DEBOUNCE_SAMPLES) { aState = aRaw; aCnt = 0; } } else aCnt = 0;
  if (bRaw != bState) { if (++bCnt >= DEBOUNCE_SAMPLES) { bState = bRaw; bCnt = 0; } } else bCnt = 0;

  if (first || aState != lastA || bState != lastB) {
    first = false; lastA = aState; lastB = bState;
    ledWrite(ledA, aState);
    ledWrite(ledB, bState);
    Serial.print(nameA); Serial.print(aState ? F("=DOWN  ") : F("=up    "));
    Serial.print(nameB); Serial.println(bState ? F("=DOWN") : F("=up"));
  }
}

// Loop board: two LEDs alternate in a railroad pattern; holding a button
// forces ITS led off. Buttons are debounced + failed I2C reads ignored.
#define RAILROAD_MS 220
void loopLoopBoard() {
  static unsigned long lastSample = 0, lastBlink = 0;
  static bool phase = false;
  static bool sState = false, eState = false;   // debounced button states
  static uint8_t sCnt = 0, eCnt = 0;
  static uint8_t lastReport = 0xFF;
  static uint8_t lastOut = 0xFF;

  // --- debounced button read ---
  if (millis() - lastSample >= SAMPLE_INTERVAL) {
    lastSample = millis();
    uint8_t gpioa;
    if (mcpReadReg(MCP_GPIOA, gpioa)) {
      bool sRaw = !(gpioa & M_LOOP_START_BTN);
      bool eRaw = !(gpioa & M_LOOP_END_BTN);
      if (sRaw != sState) { if (++sCnt >= DEBOUNCE_SAMPLES) { sState = sRaw; sCnt = 0; } } else sCnt = 0;
      if (eRaw != eState) { if (++eCnt >= DEBOUNCE_SAMPLES) { eState = eRaw; eCnt = 0; } } else eCnt = 0;
    }
  }

  // --- railroad blink phase ---
  if (millis() - lastBlink >= RAILROAD_MS) {
    lastBlink = millis();
    phase = !phase;
  }

  // alternate the two LEDs; a held button kills its own LED
  bool startOn = !phase && !sState;
  bool endOn   =  phase && !eState;

  uint8_t outBits = (startOn ? M_LOOP_START_LED : 0) | (endOn ? M_LOOP_END_LED : 0);
  if (outBits != lastOut) {
    lastOut = outBits;
    uint8_t cur;
    if (mcpReadReg(MCP_OLATB, cur)) {           // preserve play/cue LED bits
      cur = (cur & ~(M_LOOP_START_LED | M_LOOP_END_LED)) | outBits;
      mcpWrite(MCP_OLATB, cur);
    }
  }

  // report button changes
  uint8_t now = (sState ? 1 : 0) | (eState ? 2 : 0);
  if (now != lastReport) {
    lastReport = now;
    Serial.print(F("LOOP_START")); Serial.print(sState ? F("=DOWN  ") : F("=up    "));
    Serial.print(F("LOOP_END"));   Serial.println(eState ? F("=DOWN") : F("=up"));
  }
}

void loopUart() {
  // heartbeat out
  if (millis() - lastTick >= 500) {
    lastTick = millis();
    static uint32_t n = 0;
    Serial1.print(F("TriMixxx UART hello ")); Serial1.println(n++);
    Serial.println(F("  -> sent heartbeat on Serial1"));
  }
  // echo whatever the Pi (or loopback) sends back
  while (Serial1.available()) {
    char c = Serial1.read();
    Serial.print(F("  <- RX: ")); Serial.println(c);
  }
}

// ============================================================ core ==========
void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r' || c == ' ') continue;
    switch (c) {
      case 'm': mode = MENU; printMenu(); break;
      case '?': printMenu(); break;
      case '1': enterMode(T1_I2C); break;
      case '2': enterMode(T2_FADER); break;
      case '3': enterMode(T3_JOG); break;
      case '4': enterMode(T4_PLAYCUE); break;
      case '5': enterMode(T5_LOOP); break;
      case '6': enterMode(T6_UART); break;
      default:  Serial.print(F("? unknown: ")); Serial.println(c); break;
    }
  }
}

void setup() {
  Serial.begin(USB_BAUD);
  Serial1.begin(UART_BAUD);
  Wire.begin();
  Wire.setClock(100000);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) { /* wait up to 3s for USB serial */ }
  printMenu();
}

void loop() {
  handleSerial();
  switch (mode) {
    case T2_FADER:   loopFader(); break;
    case T3_JOG:     loopJog();   break;
    case T4_PLAYCUE: loopButtons(M_BTN_PLAY, M_LED_PLAY, F("PLAY"),
                                 M_BTN_CUE,  M_LED_CUE,  F("CUE")); break;
    case T5_LOOP:    loopLoopBoard(); break;
    case T6_UART:    loopUart();  break;
    default: break;
  }
}