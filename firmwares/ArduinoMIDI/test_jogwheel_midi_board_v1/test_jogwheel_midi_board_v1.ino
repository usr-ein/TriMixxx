/* jogwheel_test_midi_v1
 * ---------------------------------------------------------------
 * Dead-simple raw pin probe for the jog wheel. No Encoder library,
 * no interrupts, no decoding - just reads the three pins and prints
 * whenever ANY of them changes. Use this to confirm signals actually
 * reach the Arduino pins.
 *
 * Board: ATmega32U4 Pro Micro  (Arduino IDE board = "Arduino Leonardo")
 * Pins:  TCH  -> D7 (PE6)
 *        JOG1 -> D8 (PB4)
 *        JOG2 -> D9 (PB5)
 *        +5V, GND to the jog connector
 *
 * Open Serial Monitor @ 115200.
 *   - Spin the platter slowly: JOG1/JOG2 should toggle 0/1.
 *   - Press the platter top:   TCH should change (active-LOW).
 * If nothing changes -> the signal isn't reaching the pin (wiring,
 * connector, ground, or power), not a software problem.
 * ---------------------------------------------------------------
 */

#define PIN_TCH   7   // PE6
#define PIN_JOG1  8   // PB4
#define PIN_JOG2  9   // PB5

int lastT = -1, last1 = -1, last2 = -1;
unsigned long lastBeat = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }
  delay(300);

  // Internal pull-ups on the encoder pins so a switch-to-GND encoder
  // reads cleanly. TCH has its own 22k pull-up on the JOGB PCB.
  pinMode(PIN_JOG1, INPUT_PULLUP);
  pinMode(PIN_JOG2, INPUT_PULLUP);
  pinMode(PIN_TCH,  INPUT_PULLUP);   // pull-up is harmless even with the board's pull-up

  Serial.println(F("jogwheel_test_midi_v1 - raw pin probe"));
  Serial.println(F("TCH=D7  JOG1=D8  JOG2=D9   (spin / press to see changes)"));
  Serial.println(F("-------------------------------------------------"));
}

void report(const char* why) {
  Serial.print(why);
  Serial.print(F("  JOG1(D8)=")); Serial.print(last1);
  Serial.print(F("  JOG2(D9)=")); Serial.print(last2);
  Serial.print(F("  TCH(D7)="));  Serial.print(lastT);
  Serial.println();
}

void loop() {
  int t  = digitalRead(PIN_TCH);
  int j1 = digitalRead(PIN_JOG1);
  int j2 = digitalRead(PIN_JOG2);

  if (j1 != last1 || j2 != last2 || t != lastT) {
    last1 = j1; last2 = j2; lastT = t;
    report("CHANGE ");
  }

  // heartbeat every 2s so you know the sketch is alive and what the
  // idle levels are (helps spot a pin stuck HIGH or LOW)
  if (millis() - lastBeat > 2000) {
    lastBeat = millis();
    last1 = j1; last2 = j2; lastT = t;
    report("idle   ");
  }
}