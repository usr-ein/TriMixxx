#include <Arduino.h>
#include "OneButtonRing.hpp"
#include "PiLink.hpp"
#include "MidiMap.hpp"

// ===========================================================================
//  TriMixxx S3 master
//
//  UART allocation on the ESP32-S3:
//    UART0 (Serial0) -> Raspberry Pi   (IO43 TX, IO44 RX)  -- MIDI, 3.3V, no shifter
//    UART1 (Serial1) -> OneButton ring A                   -- 5V, needs shifter
//    UART2           -> OneButton ring B (v2)              -- 5V, needs shifter
//
//  Ring pads are wired to MIDI now. The other five subsystems (encoder, jog,
//  tempo, play/cue, loop) have their MIDI addresses reserved in MidiMap and
//  their Mixxx bindings ready -- they just need their driver modules built
//  (pinouts pending), then they call pi.noteOn/cc with the reserved addresses.
// ===========================================================================

// ---- Ring A ---------------------------------------------------------------
#define RING_A_TX 17
#define RING_A_RX 15
#define RING_A_NODES 50
OneButtonRing ringA(Serial1, RING_A_TX, RING_A_RX, RING_A_NODES, 1000000, /*core=*/0);

// ---- Ring B (v2, wired, begin() commented) --------------------------------
HardwareSerial RingBSerial(2);
#define RING_B_TX 13
#define RING_B_RX 12
OneButtonRing ringB(RingBSerial, RING_B_TX, RING_B_RX, 50, 1000000, /*core=*/0);

// ---- Pi MIDI link on UART0 ------------------------------------------------
#define PI_TX 43
#define PI_RX 44
PiLink pi(Serial0, PI_TX, PI_RX, 115200);

// per-pad note bookkeeping for clean note-on/off (sticky-safe, fast-tap-safe)
static bool padPendingOff[RING_A_NODES] = { false };

// -------- incoming MIDI from Mixxx -> LEDs (Mixxx owns LED state) ----------
static void onMidiFromMixxx(uint8_t status, uint8_t d1, uint8_t d2, void* ctx) {
    uint8_t type = status & 0xF0;

    // pad LEDs: Mixxx sends NOTE_ON on the pad's note; velocity = brightness.
    // (vel 0 = off.) v1 is white-only; RGB would use 3 CCs per LED instead.
    if (type == 0x90 && d1 >= midimap::PAD_BASE &&
        d1 < midimap::PAD_BASE + RING_A_NODES) {
        uint8_t pad = d1 - midimap::PAD_BASE;
        ringA.setLed(pad, 0, d2, d2, d2);
        return;
    }

    // TODO: play/cue LEDs  (NOTE_PLAY / NOTE_CUE)  -> GPIO on the play/cue board
    // TODO: loop LEDs      (NOTE_LOOP_IN / _OUT)   -> GPIO on the loop board
}

void setup() {
    Serial.begin(115200);                       // USB-CDC console
    delay(300);
    Serial.println("TriMixxx S3 master boot");

    pi.begin();
    pi.onMidi(onMidiFromMixxx, nullptr);

    if (!ringA.begin()) Serial.println("ringA: allocation failed");
    // ringB.begin();

    // TODO: init encoder, jog PCNT, tempo ADC, play/cue, loop board
}

void loop() {
    pi.poll();                                  // drain incoming MIDI -> LEDs

    // ---- ring pads -> MIDI notes ----
    for (uint8_t i = 0; i < RING_A_NODES; i++) {
        bool held = ringA.level(i);
        if (ringA.pressed(i)) {                 // press edge (catches fast taps)
            pi.noteOn(midimap::PAD_BASE + i, 127);
            padPendingOff[i] = true;
        }
        if (padPendingOff[i] && !held) {        // release (or tap already over)
            pi.noteOff(midimap::PAD_BASE + i);
            padPendingOff[i] = false;
        }
    }

    // ---- the other controls send MIDI here once their drivers exist ----
    // TrackEncoder: on detent  -> pi.cc(midimap::CC_ENCODER, up ? 1 : 127);
    //               on press   -> pi.noteOn(midimap::NOTE_ENC_SW, 127); (+ noteOff)
    // JogWheel:     per tick    -> pi.cc(midimap::CC_JOG, d>0 ? d : 128+d);
    //               touch       -> pi.noteOn/Off(midimap::NOTE_JOG_TOUCH, ...)
    // TempoFader:   on change   -> pi.cc(midimap::CC_TEMPO, value0_127);
    // PlayCue:      buttons      -> pi.noteOn/Off(midimap::NOTE_PLAY / NOTE_CUE)
    // Loop:         buttons      -> pi.noteOn/Off(midimap::NOTE_LOOP_IN/_OUT/RELOOP)

    static uint32_t t = 0;
    if (millis() - t > 1000) {
        t = millis();
        Serial.printf("ringA: nodes=%u link=%d good=%lu bad=%lu\n",
                      ringA.enumeratedNodes(), ringA.linkOk(),
                      (unsigned long)ringA.goodFrames(),
                      (unsigned long)ringA.badFrames());
    }

    delay(2);
}