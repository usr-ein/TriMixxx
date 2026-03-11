/*
 * CDJ-1000 MK3 Jog Wheel + Touch Sensor Test
 * =============================================
 * Board:  Arduino Pro Micro (ATmega32U4, 5V/16MHz)
 * Wiring: JOG1 → D2 (INT1 — hardware interrupt)
 *         JOG2 → D3 (INT0 — hardware interrupt)
 *         TCH  → D4 (active LOW, pull-up on JOGB PCB)
 *         V+5  → VCC (5V)
 *         GND  → GND
 *
 * Both encoder pins use hardware interrupts — no missed
 * pulses even at full scratch speed.
 *
 * Touch sensor is a pressure-sensitive sheet switch (DSX1065)
 * with 22kΩ pull-up (R2007) and 22nF debounce cap (C2002)
 * already on the JOGB PCB. TCH is HIGH when not touched,
 * LOW when platter is pressed down.
 *
 * Install: Sketch > Include Library > Manage Libraries >
 *          search "Encoder" by Paul Stoffregen (PJRC)
 *
 * Flash:  Board = "Arduino Leonardo", then Upload as normal.
 *         Rename this file to .ino or add it to an .ino
 *         wrapper if your IDE requires it.
 */

#include <Encoder.h>

// --- Pin Definitions ---
#define JOG1_PIN 2
#define JOG2_PIN 3
#define TOUCH_PIN 4

// --- Encoder Object ---
Encoder jogWheel(JOG1_PIN, JOG2_PIN);

// --- Known PPR (measured: 13000 ticks / 4x decoding = 3250) ---
#define KNOWN_PPR 3250

// --- State Variables ---
long oldPosition  = 0;
unsigned long lastPrintTime = 0;
unsigned long lastChangeTime = 0;

// Touch state
bool lastTouchState = false;

// Rolling velocity calculation
long lastVelocityPos = 0;
unsigned long lastVelocityTime = 0;
const unsigned long VELOCITY_INTERVAL_MS = 50;

// Statistics
long minPosition = 0;
long maxPosition = 0;
unsigned long totalTicks = 0;
bool firstRevolution = true;
long revolutionStartPos = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ; // Wait for serial port (Pro Micro native USB)
  }
  delay(500);

  // Touch pin — external pull-up on JOGB PCB (R2007 22kΩ to 5V)
  pinMode(TOUCH_PIN, INPUT);

  Serial.println(F("========================================"));
  Serial.println(F("  CDJ-1000 MK3 Jog Wheel + Touch Test"));
  Serial.println(F("  Pro Micro (ATmega32U4)"));
  Serial.println(F("========================================"));
  Serial.println(F("Wiring: JOG1→D2, JOG2→D3, TCH→D4, 5V, GND"));
  Serial.println(F(""));
  Serial.println(F("Encoder: hardware interrupts — full speed."));
  Serial.println(F("Touch:   active LOW (press platter to trigger)"));
  Serial.println(F("PPR:     3250 (13000 ticks / 4x decoding)"));
  Serial.println(F(""));
  Serial.println(F("Commands (send via Serial Monitor):"));
  Serial.println(F("  'r' - Reset position counter to 0"));
  Serial.println(F("  's' - Show statistics"));
  Serial.println(F("  'p' - Start PPR measurement (rotate exactly 1 turn)"));
  Serial.println(F("  't' - Read touch sensor state"));
  Serial.println(F(""));
  Serial.println(F("Rotate the jog wheel..."));
  Serial.println(F("----------------------------------------"));

  lastVelocityTime = millis();
}

void loop() {
  unsigned long now = millis();

  // --- Read Encoder ---
  long newPosition = jogWheel.read();

  // --- Read Touch Sensor ---
  bool touched = !digitalRead(TOUCH_PIN);  // Active LOW — invert

  // Print on touch state change
  if (touched != lastTouchState) {
    if (touched) {
      Serial.println(F(">>> TOUCH: PRESSED — vinyl mode <<<"));
    } else {
      Serial.println(F(">>> TOUCH: RELEASED <<<"));
    }
    lastTouchState = touched;
  }

  // --- Handle Serial Commands ---
  if (Serial.available()) {
    char cmd = Serial.read();
    switch (cmd) {
      case 'r':
      case 'R':
        jogWheel.write(0);
        oldPosition = 0;
        newPosition = 0;
        minPosition = 0;
        maxPosition = 0;
        totalTicks = 0;
        Serial.println(F("\n>>> Position RESET to 0 <<<\n"));
        break;

      case 's':
      case 'S':
        Serial.println(F("\n--- Statistics ---"));
        Serial.print(F("  Current position: ")); Serial.println(newPosition);
        Serial.print(F("  Min position:     ")); Serial.println(minPosition);
        Serial.print(F("  Max position:     ")); Serial.println(maxPosition);
        Serial.print(F("  Total range:      ")); Serial.println(maxPosition - minPosition);
        Serial.print(F("  Total ticks:      ")); Serial.println(totalTicks);
        Serial.print(F("  Touch state:      ")); Serial.println(touched ? "PRESSED" : "released");
        Serial.println(F("-----------------\n"));
        break;

      case 'p':
      case 'P':
        revolutionStartPos = newPosition;
        firstRevolution = false;
        Serial.println(F("\n>>> PPR Measurement Started <<<"));
        Serial.println(F("Slowly rotate EXACTLY one full turn, then press 'p' again."));
        Serial.print(F("Start position: ")); Serial.println(revolutionStartPos);
        break;

      case 't':
      case 'T':
        Serial.print(F("Touch sensor: "));
        Serial.print(touched ? "PRESSED" : "released");
        Serial.print(F("  (raw pin: "));
        Serial.print(digitalRead(TOUCH_PIN));
        Serial.println(F(")"));
        break;
    }
  }

  // --- Detect Encoder Changes ---
  if (newPosition != oldPosition) {
    long delta = newPosition - oldPosition;
    lastChangeTime = now;

    // Update statistics
    totalTicks += abs(delta);
    if (newPosition > maxPosition) maxPosition = newPosition;
    if (newPosition < minPosition) minPosition = newPosition;

    // Print position change
    Serial.print(F("Pos: "));
    Serial.print(newPosition);
    Serial.print(F("  Δ: "));
    Serial.print(delta);
    Serial.print(F("  Dir: "));
    Serial.print(delta > 0 ? "CW >>>" : "<<< CCW");

    // Show touch state inline when spinning
    if (touched) {
      Serial.print(F("  [SCRATCH]"));
    }

    // PPR measurement helper
    if (!firstRevolution) {
      long pprDelta = abs(newPosition - revolutionStartPos);
      Serial.print(F("  [PPR ticks: "));
      Serial.print(pprDelta);
      Serial.print(F("]"));
    }

    Serial.println();
    oldPosition = newPosition;
  }

  // --- Velocity Calculation (periodic) ---
  if (now - lastVelocityTime >= VELOCITY_INTERVAL_MS) {
    long velocityDelta = newPosition - lastVelocityPos;
    unsigned long dt = now - lastVelocityTime;

    if (velocityDelta != 0) {
      float ticksPerSec = (float)velocityDelta / dt * 1000.0;
      float rpm = (ticksPerSec / (KNOWN_PPR * 4.0)) * 60.0;

      Serial.print(F("  >> Velocity: "));
      Serial.print(ticksPerSec, 1);
      Serial.print(F(" ticks/sec  RPM: "));
      Serial.print(rpm, 2);

      if (touched) {
        Serial.print(F("  [VINYL]"));
      }

      Serial.println();
    }

    lastVelocityPos = newPosition;
    lastVelocityTime = now;
  }

  // --- Idle detection ---
  if (lastChangeTime > 0 && (now - lastChangeTime > 2000) && (now - lastPrintTime > 5000)) {
    Serial.print(F("  [idle at position "));
    Serial.print(newPosition);
    Serial.print(F(", touch: "));
    Serial.print(touched ? "PRESSED" : "released");
    Serial.println(F("]"));
    lastPrintTime = now;
  }
}