#include <Arduino.h>
#include <OneButtonRing.hpp>

// ===========================================================================
//  TriMixxx S3 master  -  firmware skeleton
//
//  UART allocation on the ESP32-S3 (3 hardware UARTs):
//    UART0  -> Raspberry Pi   (IO43 TX, IO44 RX)   [Pi-link module, TODO]
//    UART1  -> OneButton ring A
//    UART2  -> OneButton ring B   (v2)
//
//  The OneButtonRing library runs each ring in its own task. Everything else
//  (Pi link, encoder, jog PCNT, tempo fader, play/cue, loop board) lives in
//  separate modules that just read/write the ring objects -- the ring code is
//  kept entirely separate, as intended.
// ===========================================================================

// ---- Ring A (v1) -- pins per board wiring ----------------------------------
#define RING_A_TX     17     // 1BTNA_TX  (S3 -> node0 DIN, via level shifter)
#define RING_A_RX     15     // 1BTNA_RX  (nodeN DOUT -> S3, via level shifter)
#define RING_A_NODES  50

OneButtonRing ringA(Serial1, RING_A_TX, RING_A_RX, RING_A_NODES, 1000000, /*core=*/0);

// ---- Ring B -- same class, second instance on UART2 ------------------------
// Wired and ready. v1 runs one ring, so ringB.begin() is left commented in
// setup(); uncomment it to bring the second ring online. The object itself is
// harmless until begin() is called (the constructor only stores parameters).
HardwareSerial RingBSerial(2);            // UART2
#define RING_B_TX     13     // 1BTNB_TX
#define RING_B_RX     12     // 1BTNB_RX
#define RING_B_NODES  50

OneButtonRing ringB(RingBSerial, RING_B_TX, RING_B_RX, RING_B_NODES, 1000000, /*core=*/0);

// ---- The other six subsystems (separate modules; pinouts TBD) --------------
// TODO: PiLink      -- UART0 MIDI bridge to the Pi (IO43/IO44)
// TODO: TrackEncoder-- ENC_SW / ENC_DT / ENC_CLK
// TODO: JogWheel    -- PCNT on JOG1 / JOG2 + JOG_TCH
// TODO: TempoFader  -- ADC1 on TEMPO_ADCT (IO8) / TEMPO_ADIN (IO9)
// TODO: PlayCue     -- BTN_PLAY / BTN_CUE / LED_PLAY / LED_CUE
// TODO: LoopBoard   -- LOOP_START_BTN / LOOP_END_BTN / RELOOP_BTN + LEDs

void setup() {
    Serial.begin(115200);                 // USB-CDC console
    delay(300);
    Serial.println("TriMixxx S3 master boot");

    if (!ringA.begin()) Serial.println("ringA: allocation failed");
    // ringB.begin();

    // TODO: init the six other subsystems here.
}

void loop() {
    // --------------------------------------------------------------------
    //  Ring A demo: light each node's LED0 white while its button is held.
    //  This proves the ring end-to-end. Replace with real Mixxx-driven LED
    //  logic and forward button edges to the Pi as MIDI.
    // --------------------------------------------------------------------
    for (uint8_t i = 0; i < RING_A_NODES; i++) {
        uint8_t v = ringA.level(i) ? 60 : 0;
        ringA.setLed(i, 0, v, v, v);

        if (ringA.pressed(i)) {
            // clean press edge for pad i -> queue MIDI to the Pi (TODO)
        }
    }

    // status once a second
    static uint32_t t = 0;
    if (millis() - t > 1000) {
        t = millis();
        Serial.printf("ringA: nodes=%u link=%d good=%lu bad=%lu\n",
                      ringA.enumeratedNodes(), ringA.linkOk(),
                      (unsigned long)ringA.goodFrames(),
                      (unsigned long)ringA.badFrames());
    }

    // The ring runs in its own task; loop() only reads/writes ring state and
    // never blocks on it. Service the other subsystems here (or give them
    // their own tasks too).
    delay(5);
}