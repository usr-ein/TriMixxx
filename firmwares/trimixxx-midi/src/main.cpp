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
OneButtonRing ringA(Serial1, RING_A_TX, RING_A_RX, RING_A_NODES, 500000,
                    /*core=*/0); // 500 kbps: MUST match the node firmware

// ---- Ring B (v2, wired, begin() commented) --------------------------------
HardwareSerial RingBSerial(2);
#define RING_B_TX 13
#define RING_B_RX 12
OneButtonRing ringB(RingBSerial, RING_B_TX, RING_B_RX, 50, 500000,
                    /*core=*/0); // 500 kbps: MUST match the node firmware

// ---- Pi MIDI link on UART0 ------------------------------------------------
#define PI_TX 43
#define PI_RX 44
PiLink pi(Serial0, PI_TX, PI_RX, 115200);

// ---- Jog wheel (PCNT quad + touch) ----------------------------------------
#define JOG_A 6
#define JOG_B 7
#define JOG_TCH 14
// Touch is active-low (built in). Encoder needs no pull (OPIC drives it);
// set encoderPullup=true only as insurance against phantom counts if the
// jog cable is ever unplugged.
JogWheel jog(JOG_A, JOG_B, JOG_TCH, /*encoderPullup=*/false);

// ---- Tempo fader (ratiometric: center tap + wiper, both ADC1) --------------
// Wired inverted on this deck (high end = slow), so invert=true. Bench-measured
// offsets: top = -1944, bottom = +2150 (center tap is off electrical mid, hence
// the asymmetric per-side spans).
#define TEMPO_ADCT 8
#define TEMPO_ADIN 9
TempoFader tempo(TEMPO_ADCT, TEMPO_ADIN, /*spanToMax=*/1944, /*spanToMin=*/2150, /*invert=*/true);

// ---- Track encoder (KY-040 mechanical: CLK/DT/SW) --------------------------
#define ENC_CLK 33
#define ENC_DT 37
#define ENC_SW 38
TrackEncoder trackEnc(ENC_CLK, ENC_DT, ENC_SW);

static bool padPendingOff[RING_A_NODES] = {false};

// -------- incoming MIDI from Mixxx -> LEDs (Mixxx owns LED state) ----------
static void onMidiFromMixxx(uint8_t status, uint8_t d1, uint8_t d2, void* ctx) {
    uint8_t type = status & 0xF0;
    if (type == 0x90 && d1 >= midimap::PAD_BASE && d1 < midimap::PAD_BASE + RING_A_NODES) {
        uint8_t pad = d1 - midimap::PAD_BASE;
        ringA.setLed(pad, 0, d2, d2, d2); // velocity = white brightness
        return;
    }
    // TODO: play/cue LEDs (NOTE_PLAY/CUE) and loop LEDs (NOTE_LOOP_*) -> GPIO
}

// ===========================================================================
//  Ring bring-up debug. Set to 1 to run the two-node test instead of the deck:
//    - the two nodes "railroad" blink (alternate red on/off), and
//    - holding either node's button turns the OTHER node magenta.
//  The ring self-sizes to whatever enumerates, so 2 nodes just works. Set to 0
//  to restore the normal deck firmware.
// ===========================================================================
#define RING_DEBUG 1

#if RING_DEBUG
static void setNode(uint8_t node, uint8_t r, uint8_t g, uint8_t b) {
    ringA.setLed(node, 0, r, g, b); // both LEDs of the node, same colour
    ringA.setLed(node, 1, r, g, b);
}

static void ringDebug() {
    // Railroad phase toggles ~1.25 Hz; the two nodes light on opposite phases.
    static uint32_t tBlink = 0;
    static bool     phase  = false;
    if (millis() - tBlink >= 400) {
        tBlink = millis();
        phase  = !phase;
    }

    const bool held0 = ringA.level(0);
    const bool held1 = ringA.level(1);

    // Press on the OTHER node wins over the blink and paints this node magenta.
    if (held1) setNode(0, 255, 0, 255);
    else setNode(0, 0, phase ? 255 : 0, 0);
    if (held0) setNode(1, 255, 0, 255);
    else setNode(1, 0, !phase ? 255 : 0, 0);

    static uint32_t tLog = 0;
    if (millis() - tLog > 1000) {
        tLog = millis();
        Serial.printf("ring: enum=%u link=%d good=%lu bad=%lu  btn0=%d btn1=%d\n",
                      ringA.enumeratedNodes(), ringA.linkOk(), (unsigned long)ringA.goodFrames(),
                      (unsigned long)ringA.badFrames(), held0, held1);
    }
}
#endif

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("TriMixxx S3 master boot");

    pi.begin();
    pi.onMidi(onMidiFromMixxx, nullptr);

    jog.begin();
    // TEMPORARY: force a pull-up on the jog touch pin. Overrides the plain
    // INPUT that jog.begin() sets. REMOVE THIS -- the production PCB already
    // has a hardware pull-up on this line; this is only for bench bring-up on
    // boards without it.
    pinMode(JOG_TCH, INPUT_PULLUP);
    tempo.begin();
    trackEnc.begin();

    if (!ringA.begin()) Serial.println("ringA: allocation failed");
    // ringB.begin();

    // TODO: init play/cue, loop board
}

void loop() {
#if RING_DEBUG
    ringDebug(); // bench ring test -- railroad blink + press-other-magenta
    delay(5);
    return;
#endif

    pi.poll(); // incoming MIDI -> LEDs

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

    // ---- serial monitor: report jog movement + touch ----
    static int32_t jogPos = 0; // running tick count (debug only)
    if (jd != 0) {
        jogPos += jd;
        Serial.printf("jog: delta=%+ld pos=%ld touch=%d\n", (long)jd, (long)jogPos, jog.touched());
    }

    while (jd != 0) { // send full delta in <=63 chunks
        int32_t chunk = jd;
        if (chunk > 63) chunk = 63;
        if (chunk < -63) chunk = -63;
        pi.cc(midimap::CC_JOG, (uint8_t)(chunk & 0x7F)); // 7-bit two's complement
        jd -= chunk;
    }
    if (jog.touchPressed()) {
        pi.noteOn(midimap::NOTE_JOG_TOUCH, 127);
        Serial.println("jog: touch DOWN");
    }
    if (jog.touchReleased()) {
        pi.noteOff(midimap::NOTE_JOG_TOUCH);
        Serial.println("jog: touch UP");
    }

    // ---- tempo fader -> 14-bit absolute CC (MSB + LSB, only on change) ----
    tempo.poll();
    if (tempo.changed()) {
        uint16_t v = tempo.value();                        // 0..16383
        pi.cc(midimap::CC_TEMPO, (uint8_t)(v >> 7));       // high 7 bits (MSB)
        pi.cc(midimap::CC_TEMPO_LSB, (uint8_t)(v & 0x7F)); // low 7 bits (LSB)
    }

    // ---- track encoder -> relative CC (1=up / 127=down) + press note ----
    trackEnc.poll();
    int8_t ed = trackEnc.readDelta();
    for (; ed > 0; ed--) pi.cc(midimap::CC_ENCODER, 1);   // one detent up
    for (; ed < 0; ed++) pi.cc(midimap::CC_ENCODER, 127); // one detent down
    if (trackEnc.switchPressed()) pi.noteOn(midimap::NOTE_ENC_SW, 127);
    if (trackEnc.switchReleased()) pi.noteOff(midimap::NOTE_ENC_SW);

    // ---- remaining controls send MIDI here once their drivers exist ----
    // PlayCue: buttons -> pi.noteOn/Off(NOTE_PLAY / NOTE_CUE)
    // Loop:    buttons -> pi.noteOn/Off(NOTE_LOOP_IN/_OUT/RELOOP)

    // ---- tempo fader calibration readout ----
    // Watch center/wiper/offset while moving the fader to both extremes to pick
    // the `span` ctor arg (offset at full throw) and confirm the detent centers.
    static uint32_t t = 0;
    if (millis() - t > 200) {
        t = millis();
        Serial.printf("tempo: center=%u wiper=%u offset=%+d value=%u\n", tempo.center(),
                      tempo.wiper(), tempo.offset(), tempo.value());
    }

    delay(2);
}