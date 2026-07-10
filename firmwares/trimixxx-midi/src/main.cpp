#include <Arduino.h>
#include "OneButtonRing.hpp"
#include "PiLink.hpp"
#include "JogWheel.hpp"
#include "TempoFader.hpp"
#include "TrackEncoder.hpp"
#include "MidiMap.hpp"

// ===========================================================================
//  TriMixxx S3 master
//
//  UART allocation on the ESP32-S3:
//    UART0 (Serial0) -> Raspberry Pi   (IO43 TX, IO44 RX)  -- MIDI, 3.3V
//    UART1 (Serial1) -> OneButton ring A                   -- 5V, needs shifter
//    UART2           -> OneButton ring B (v2)              -- 5V, needs shifter
//
//  Implemented: ring pads (MIDI in/out) + jog wheel (PCNT). The remaining four
//  controls (encoder, tempo, play/cue, loop) have reserved MIDI addresses and
//  live Mixxx bindings -- they just need their driver modules built.
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

// ---- Jog wheel (PCNT quad + touch) ----------------------------------------
#define JOG_A   6
#define JOG_B   7
#define JOG_TCH 14
// Touch is active-low (built in). Encoder needs no pull (OPIC drives it);
// set encoderPullup=true only as insurance against phantom counts if the
// jog cable is ever unplugged.
JogWheel jog(JOG_A, JOG_B, JOG_TCH, /*encoderPullup=*/false);

// ---- Tempo fader (ratiometric: center tap + wiper, both ADC1) --------------
#define TEMPO_ADCT 8
#define TEMPO_ADIN 9
TempoFader tempo(TEMPO_ADCT, TEMPO_ADIN);

// ---- Track encoder (KY-040 mechanical: CLK/DT/SW) --------------------------
#define ENC_CLK 33
#define ENC_DT  37
#define ENC_SW  38
TrackEncoder trackEnc(ENC_CLK, ENC_DT, ENC_SW);

static bool padPendingOff[RING_A_NODES] = { false };

// -------- incoming MIDI from Mixxx -> LEDs (Mixxx owns LED state) ----------
static void onMidiFromMixxx(uint8_t status, uint8_t d1, uint8_t d2, void* ctx) {
    uint8_t type = status & 0xF0;
    if (type == 0x90 && d1 >= midimap::PAD_BASE &&
        d1 < midimap::PAD_BASE + RING_A_NODES) {
        uint8_t pad = d1 - midimap::PAD_BASE;
        ringA.setLed(pad, 0, d2, d2, d2);          // velocity = white brightness
        return;
    }
    // TODO: play/cue LEDs (NOTE_PLAY/CUE) and loop LEDs (NOTE_LOOP_*) -> GPIO
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("TriMixxx S3 master boot");

    pi.begin();
    pi.onMidi(onMidiFromMixxx, nullptr);

    jog.begin();
    tempo.begin();
    trackEnc.begin();

    if (!ringA.begin()) Serial.println("ringA: allocation failed");
    // ringB.begin();

    // TODO: init play/cue, loop board
}

void loop() {
    pi.poll();                                     // incoming MIDI -> LEDs

    // ---- ring pads -> MIDI notes ----
    for (uint8_t i = 0; i < RING_A_NODES; i++) {
        bool held = ringA.level(i);
        if (ringA.pressed(i)) {
            pi.noteOn(midimap::PAD_BASE + i, 127);
            padPendingOff[i] = true;
        }
        if (padPendingOff[i] && !held) {
            pi.noteOff(midimap::PAD_BASE + i);
            padPendingOff[i] = false;
        }
    }

    // ---- jog wheel -> relative CC (+ touch -> scratch enable note) ----
    jog.poll();
    int32_t jd = jog.readDelta();
    while (jd != 0) {                               // send full delta in <=63 chunks
        int32_t chunk = jd;
        if (chunk >  63) chunk =  63;
        if (chunk < -63) chunk = -63;
        pi.cc(midimap::CC_JOG, (uint8_t)(chunk & 0x7F));   // 7-bit two's complement
        jd -= chunk;
    }
    if (jog.touchPressed())  pi.noteOn (midimap::NOTE_JOG_TOUCH, 127);
    if (jog.touchReleased()) pi.noteOff(midimap::NOTE_JOG_TOUCH);

    // ---- tempo fader -> absolute CC (only on change) ----
    tempo.poll();
    if (tempo.changed()) pi.cc(midimap::CC_TEMPO, tempo.value());

    // ---- track encoder -> relative CC (1=up / 127=down) + press note ----
    trackEnc.poll();
    int8_t ed = trackEnc.readDelta();
    for (; ed > 0; ed--) pi.cc(midimap::CC_ENCODER, 1);      // one detent up
    for (; ed < 0; ed++) pi.cc(midimap::CC_ENCODER, 127);    // one detent down
    if (trackEnc.switchPressed())  pi.noteOn (midimap::NOTE_ENC_SW, 127);
    if (trackEnc.switchReleased()) pi.noteOff(midimap::NOTE_ENC_SW);

    // ---- remaining controls send MIDI here once their drivers exist ----
    // PlayCue: buttons -> pi.noteOn/Off(NOTE_PLAY / NOTE_CUE)
    // Loop:    buttons -> pi.noteOn/Off(NOTE_LOOP_IN/_OUT/RELOOP)

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