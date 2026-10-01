#include <Arduino.h>
#include <esp_system.h>
#include <string.h>
#include "OneButtonRing.hpp"
#include "PiLink.hpp"
#include "JogWheel.hpp"
#include "TempoFader.hpp"
#include "TrackEncoder.hpp"
#include "PlayCueBoard.hpp"
#include "LoopBoard.hpp"
#include "MidiMap.hpp"

// ===========================================================================
//  TriMixxx S3 master
//
//  UART allocation on the ESP32-S3:
//    UART0 (Serial0) -> Raspberry Pi   (IO43 TX, IO44 RX)  -- MIDI, 3.3V
//    UART1 (Serial1) -> OneButton ring A                   -- 5V, needs shifter
//    UART2 (Serial2) -> OneButton ring B  (IO13 TX, IO12 RX)  -- 5V, needs shifter
//
//  Every control has a driver: ring pads, jog, tempo, track encoder, play/cue,
//  loop. main wires them to MIDI; each module self-tests via debug() (below).
// ===========================================================================

// ===========================================================================
//  DECK_DEBUG: 1 = run each module's self-test in loop() instead of sending
//  MIDI -- ring railroad + magenta, board LED flash (solid while pressed),
//  jog/tempo/encoder serial reports. 0 = normal deck operation.
// ===========================================================================
#define DECK_DEBUG 0

// ---- Ring A ---------------------------------------------------------------
#define RING_A_TX 17
#define RING_A_RX 15
#define RING_A_NODES 50
OneButtonRing ringA(Serial1, RING_A_TX, RING_A_RX, RING_A_NODES, 500000,
                    /*core=*/0); // 500 kbps: MUST match the node firmware

// ---- Ring B (UART2) -------------------------------------------------------
#define RING_B_TX 13
#define RING_B_RX 12
#define RING_B_NODES 50
OneButtonRing ringB(Serial2, RING_B_TX, RING_B_RX, RING_B_NODES, 500000,
                    /*core=*/0); // shares core 0 with ring A; both block on UART + yield
// Ring B pads/LEDs are on the MIDI bus like ring A: pad notes at PAD_B_BASE, and
// RGB via SysEx cmd 0x03. The Mixxx mapping must add matching addresses.

// ---- Pi MIDI link on UART0 ------------------------------------------------
#define PI_TX 43
#define PI_RX 44
PiLink pi(Serial0, PI_TX, PI_RX, 115200);

// ---- Jog wheel (PCNT quad + touch) ----------------------------------------
#define JOG_A 6
#define JOG_B 7
#define JOG_TCH 11
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

// ---- Play/cue + loop boards (direct GPIO; pins baked into the drivers) ------
PlayCueBoard playCue;
LoopBoard    loopBoard;

static bool padPendingOff[RING_A_NODES]  = {false};
static bool padPendingOffB[RING_B_NODES] = {false};

// -------- incoming MIDI from Mixxx -> LEDs (Mixxx owns LED state) ----------
static void onMidiFromMixxx(uint8_t status, uint8_t d1, uint8_t d2, void* ctx) {
    uint8_t type = status & 0xF0;
    if (type == 0x90 && d1 >= midimap::PAD_A_BASE && d1 < midimap::PAD_A_BASE + RING_A_NODES) {
        uint8_t pad = d1 - midimap::PAD_A_BASE;
        ringA.setLed(pad, 0, d2, d2, d2); // velocity = white brightness
        return;
    }
    if (type == 0x90 && d1 >= midimap::PAD_B_BASE && d1 < midimap::PAD_B_BASE + RING_B_NODES) {
        uint8_t pad = d1 - midimap::PAD_B_BASE;
        ringB.setLed(pad, 0, d2, d2, d2); // velocity = white brightness
        return;
    }
    // Play/cue + loop LEDs: Note-On velocity>0 = on, Note-Off (or vel 0) = off.
    if (type == 0x90 || type == 0x80) {
        const bool on = (type == 0x90 && d2 > 0);
        switch (d1) {
        case midimap::NOTE_PLAY: playCue.setLed(PlayCueBoard::PLAY, on); break;
        case midimap::NOTE_CUE: playCue.setLed(PlayCueBoard::CUE, on); break;
        case midimap::NOTE_LOOP_IN: loopBoard.setLed(LoopBoard::LOOP_START, on); break;
        case midimap::NOTE_LOOP_OUT: loopBoard.setLed(LoopBoard::LOOP_END, on); break;
        case midimap::NOTE_RELOOP: loopBoard.setLed(LoopBoard::RELOOP, on); break; // no LED
        default: break;
        }
    }
}

// -------- incoming SysEx from Mixxx ---------------------------------------
//  F0 SYSEX_MFR_ID CMD <args...> F7  (payload here has the markers stripped).
//  See MidiMap.hpp for the layouts. Colour channels arrive as nibble pairs so
//  each one keeps its full 0..255 range despite SysEx's 7-bit data limit.
static inline uint8_t nib(const uint8_t* p) { return (uint8_t)((p[0] << 4) | (p[1] & 0x0F)); }

// Apply a ring-LED SysEx (cmd 0x01 = ring A, 0x03 = ring B) to one ring. args[0]
// = node; then colour nibble-pairs R,G,B -- 6 args = one colour on both LEDs, 12
// = LED0 then LED1 independently. Wrong length or node out of range = ignored.
static void applyRingLed(OneButtonRing& ring, uint8_t maxNodes, const uint8_t* args, uint8_t n) {
    if (n != midimap::SYSEX_RING_LED_ARGS_ONE && n != midimap::SYSEX_RING_LED_ARGS_TWO) return;
    const uint8_t node = args[0];
    if (node >= maxNodes) return;
    const uint8_t r1 = nib(args + 1), g1 = nib(args + 3), b1 = nib(args + 5);
    ring.setLed(node, 0, r1, g1, b1);
    if (n == midimap::SYSEX_RING_LED_ARGS_ONE) ring.setLed(node, 1, r1, g1, b1); // mirror onto LED1
    else ring.setLed(node, 1, nib(args + 7), nib(args + 9), nib(args + 11));
}

static void onSysExFromMixxx(const uint8_t* payload, uint8_t len, void* ctx) {
    if (len < 2 || payload[0] != midimap::SYSEX_MFR_ID) return; // not ours
    const uint8_t  cmd  = payload[1];
    const uint8_t* args = payload + 2;
    const uint8_t  n    = len - 2;

    switch (cmd) {
    case midimap::SYSEX_CMD_RING_LED: applyRingLed(ringA, RING_A_NODES, args, n); return;
    case midimap::SYSEX_CMD_RING_B_LED: applyRingLed(ringB, RING_B_NODES, args, n); return;
    case midimap::SYSEX_CMD_RESET: {
        // Magic-gated so a stray/corrupt SysEx can never reboot the deck mid-set.
        if (n != sizeof(midimap::SYSEX_RESET_MAGIC)) return;
        if (memcmp(args, midimap::SYSEX_RESET_MAGIC, n) != 0) return;
        Serial.println("SysEx reset -> rebooting");
        Serial.flush();
        Serial0.flush(); // let any in-flight MIDI out before the UART dies
        delay(50);
        esp_restart(); // same effect as the RESET button; does not return
        return;
    }
    default: return;
    }
}

// Periodic button sampling: poll+debounce+latch both boards every BTN_POLL_MS on
// a task pinned to core 1 (off the ring's core 0). Deterministic and independent
// of loop() load; a debounced press is latched so it is never missed.
static constexpr uint32_t BTN_POLL_MS = 2;
static void               buttonPollTask(void*) {
    for (;;) {
        playCue.poll();
        loopBoard.poll();
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
    }
}

// The tempo read is ~2ms of oversampled analogRead -- far too heavy for core 1's
// free-running loop, where it throttled the 300us jog to ~2ms. Run it in its own
// task pinned to CORE 0 (the ring's core, which mostly blocks on UART and yields
// every pass) at a priority BELOW the ring (5), so it fills core-0 slack and
// never touches core 1. loop() just consumes the latched value/changed().
static void tempoPollTask(void*) {
    for (;;) {
        tempo.poll();
        vTaskDelay(pdMS_TO_TICKS(4)); // ~6ms cadence (poll ~2ms + 4ms) -- ample for a fader
    }
}

// Ring pads -> MIDI: Note-On on a latched press, Note-Off when released. Nodes
// past the ring's enumerated count read !pressed/!level, so unused pads never
// fire. Shared by both rings; each has its own note base + pending array.
static void ringPadsToMidi(OneButtonRing& ring, uint8_t base, uint8_t count, bool* pending) {
    for (uint8_t i = 0; i < count; i++) {
        const bool held = ring.level(i);
        if (ring.pressed(i)) {
            pi.noteOn(base + i, 127);
            pending[i] = true;
        }
        if (pending[i] && !held) {
            pi.noteOff(base + i);
            pending[i] = false;
        }
    }
}

// Send Note-On on a latched press, Note-Off once the button is no longer held.
// Same never-miss pattern as the ring pads: pressed() is latched by the poll
// task, so even a tap between two loop passes still fires a paired On/Off here.
static void btnToNote(bool press, bool held, uint8_t note, bool& pending) {
    if (press) {
        pi.noteOn(note, 127);
        pending = true;
    }
    if (pending && !held) {
        pi.noteOff(note);
        pending = false;
    }
}

// ===========================================================================
//  Status LED. The on-board LED (GPIO47) is an addressable WS2812, so it can be
//  ANY colour -- the palette below is free to change (0..255 per channel).
//
//    * boot        -> PURPLE flash, in both modes: the deck came up.
//    * DECK_DEBUG  -> ORANGE blink: the module self-tests are running.
//    * normal      -> a PURPLE heartbeat pulse on a fixed beat (deck is alive),
//                     OVERRIDDEN by Pi-link activity: GREEN = TX (deck -> Pi),
//                     YELLOW = RX (Pi -> deck). Under a scratch the jog streams
//                     every JOG_REPORT_US so green stays solid (busy-Ethernet
//                     look), then it falls back to the purple heartbeat.
//
//  Only ONE LED, so the two directions share it: the newest event wins, and TX
//  takes it when both land in the same pass. Under a scratch that means TX
//  mostly masks RX -- unavoidable with a single LED.
//
//  CRITICAL -- rate-limit the WS2812 write. neopixelWrite() on this core is the
//  LEGACY blocking-RMT path (rmtInit/rmtWriteBlocking): each call blocks ~30us
//  with interrupt-sensitive timing. Driving it every pass of the free-running
//  loop hammers that driver -- back-to-back writes when TX/RX flip the colour
//  mask interrupts long enough to trip the INT WDT on core 1 (panic), and the
//  disrupted timing paints cyan instead of green. So we DECIDE the colour every
//  pass (cheap: just a pointer) but only PUSH it to the LED at most every
//  LED_REFRESH_MS, and only when it actually changed. A sustained scratch then
//  costs ZERO writes (colour stable = green), and transitions are capped at
//  ~50 Hz. Keep the levels low -- this LED is glaring near full scale.
// ===========================================================================
static constexpr uint32_t HEARTBEAT_MS   = 500; // DECK_DEBUG blink half-period -> 1 Hz
static constexpr uint32_t ACT_FLASH_MS   = 40;  // hold an activity colour this long
static constexpr uint32_t LED_REFRESH_MS = 20;  // min gap between RMT writes -> 50 Hz cap
static constexpr uint32_t BOOT_FLASH_MS  = 250;
static constexpr uint32_t HB_PERIOD_MS   = 1000; // normal-mode purple heartbeat period
static constexpr uint32_t HB_ON_MS       = 100;  // ... LED lit for this slice of each period

// Dim palette. Yellow is R+G balanced, orange is red-dominant, so the two stay
// clearly distinct despite both being "warm".
static constexpr uint8_t LED_OFF[3]    = {0, 0, 0};
static constexpr uint8_t LED_PURPLE[3] = {16, 0, 16};
static constexpr uint8_t LED_ORANGE[3] = {24, 5, 0};
static constexpr uint8_t LED_GREEN[3]  = {0, 16, 0};
static constexpr uint8_t LED_YELLOW[3] = {14, 14, 0};

// The LOLIN S3 Mini's on-board LED is wired RGB, but neopixelWrite() assumes a
// GRB WS2812 (it sends green first), which swaps red and green on this board --
// bench-confirmed: purple came out cyan and orange came out green. Undo it by
// handing neopixelWrite our GREEN as its "red" arg and our RED as its "green".
static void ledWrite(const uint8_t rgb[3]) {
    neopixelWrite(RGB_BUILTIN, rgb[1], rgb[0], rgb[2]); // logical (R,G,B) -> LED's RGB order
}

// Colour we WANT shown; set every pass, pushed to the LED by ledFlush(). Palette
// arrays are compared by identity, so always assign one of the LED_* arrays.
static const uint8_t* g_ledWant = LED_OFF;

// Push g_ledWant to the WS2812 -- rate-limited and change-only (see the block
// comment). This is the ONLY caller of neopixelWrite() in the running loop.
static void ledFlush() {
    static const uint8_t* shown  = nullptr;
    static uint32_t       lastMs = 0;
    if (g_ledWant == shown) return; // no change -> no write (stable colour is free)
    const uint32_t nowMs = millis();
    if ((uint32_t)(nowMs - lastMs) < LED_REFRESH_MS) return; // cap the RMT write rate
    lastMs = nowMs;
    shown  = g_ledWant;
    ledWrite(shown);
}

// Boot flash. setup() only (before the loop spins), so a blocking write is free.
static void ledBootFlash() {
    ledWrite(LED_PURPLE);
    delay(BOOT_FLASH_MS);
    ledWrite(LED_OFF);
}

static void ledPoll() {
    const uint32_t nowMs = millis();
#if DECK_DEBUG
    static uint32_t lastMs = 0;
    static bool     on     = false;
    if ((uint32_t)(nowMs - lastMs) >= HEARTBEAT_MS) { // unsigned: rollover-safe
        lastMs    = nowMs;
        on        = !on;
        g_ledWant = on ? LED_ORANGE : LED_OFF;
    }
#else
    // Purple heartbeat by default; MIDI activity overrides it for ACT_FLASH_MS.
    // Consume BOTH activity flags every pass (consume-on-read).
    static const uint8_t* actCol   = LED_OFF;
    static uint32_t       actUntil = 0;
    const bool            tx       = pi.tookTx();
    const bool            rx       = pi.tookRx();
    if (tx || rx) {
        actCol   = tx ? LED_GREEN : LED_YELLOW; // TX wins when both land together
        actUntil = nowMs + ACT_FLASH_MS;
    }
    if ((int32_t)(actUntil - nowMs) > 0) {
        g_ledWant = actCol; // an activity flash is still showing
    } else {
        g_ledWant = (nowMs % HB_PERIOD_MS) < HB_ON_MS ? LED_PURPLE : LED_OFF; // heartbeat
    }
#endif
    ledFlush(); // the single rate-limited physical write
}

// ===========================================================================
//  USB-CDC console (normal mode). USB-CDC drops anything printed before the host
//  opens the port, so the boot banner is normally lost -- print it on the CONNECT
//  EDGE instead (when a monitor actually attaches), then low-rate stats. Every
//  console write is gated on (bool)Serial == host attached, so a headless deck
//  prints nothing and the loop never blocks on a full CDC buffer. g_loops counts
//  loop() passes so we can report the free-run rate. All very low priority: it
//  self-gates to STATS_MS and touches only USB-CDC, never the Pi MIDI UART.
// ===========================================================================
static constexpr uint32_t STATS_MS = 2000;
static uint32_t           g_loops  = 0; // ++ every loop() pass; sampled for loop-Hz

static const char* resetReasonStr() {
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    default: return "other";
    }
}

static void printWelcome() {
    Serial.println();
    Serial.println("======================================");
    Serial.println("  TriMixxx S3 master -- normal mode");
    Serial.printf("  build : %s %s\n", __DATE__, __TIME__);
    Serial.printf("  reset : %s\n", resetReasonStr());
    Serial.printf("  heap  : %u B free\n", (unsigned)ESP.getFreeHeap());
    Serial.println("======================================");
}

static void serialPoll() {
    if (!(bool)Serial) return; // headless -- or a momentary CDC flap: skip, never block

    // Banner ONCE per boot, latched. HWCDC's "connected" flag flaps (it drops on
    // a TX timeout and re-raises on the next successful send), so edge-triggering
    // the banner reprints it every few seconds -- latch instead of edge-detect.
    static bool welcomed = false;
    if (!welcomed) {
        printWelcome();
        welcomed = true;
    }

    static uint32_t lastMs    = 0;
    static uint32_t lastLoops = 0;
    const uint32_t  nowMs     = millis();
    const uint32_t  dt        = nowMs - lastMs;
    if (dt < STATS_MS) return;
    const uint32_t loops = g_loops - lastLoops;
    lastMs               = nowMs;
    lastLoops            = g_loops;
    Serial.printf("[stat] up=%us loop=%uHz heap=%u min=%u\n", (unsigned)(nowMs / 1000),
                  (unsigned)(dt ? (uint32_t)((uint64_t)loops * 1000 / dt) : 0),
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("TriMixxx S3 master boot");
    ledBootFlash(); // purple: the deck came up (both modes)

    pi.begin();
    pi.onMidi(onMidiFromMixxx, nullptr);
    pi.onSysEx(onSysExFromMixxx, nullptr);

    jog.begin();
    tempo.begin();
    trackEnc.begin();

    if (!ringA.begin()) Serial.println("ringA: allocation failed");
    if (!ringB.begin()) Serial.println("ringB: allocation failed");

    playCue.begin();
    loopBoard.begin();
    // Periodic poll+debounce+latch for both boards, pinned to core 1.
    xTaskCreatePinnedToCore(buttonPollTask, "btn_poll", 2048, nullptr, 3, nullptr, 1);
    // Tempo ADC read on core 0, prio 2 (below the ring's 5), off the jog loop.
    xTaskCreatePinnedToCore(tempoPollTask, "tempo_poll", 2048, nullptr, 2, nullptr, 0);
}

// ===========================================================================
//  loop() cadence. loop() free-runs -- no delay() -- and gates each job on its
//  own deadline. Arduino's loopTask is pinned to core 1, whose idle task is NOT
//  watchdog-checked in this build (core 0's IS -- and that is the ring's core),
//  so spinning here is safe. Two deadlines:
//
//   * JOG_REPORT_US -- jog rotation only. The UART is the hard limit: 115200
//     8N1 = 11520 B/s, and a jog CC is 3 bytes (ttymidi does not decode running
//     status). One CC per 300us = ~10 kB/s ~= 87% of the link during a sustained
//     fast scratch, leaving ~13% for tempo/encoder/button traffic. 260us would
//     be 100% -- never go below it. This deliberately runs near the ceiling: if
//     the 128-byte TX FIFO fills, uart_write_bytes() blocks, the loop stalls and
//     the next deadline slips, so back off toward ~400us (~65%) if jog timing
//     ever feels uneven under load.
//   * CTRL_POLL_MS -- everything human-scale (pads, jog touch, tempo, encoder,
//     buttons). A finger press is tens of ms; none of it belongs at kHz rates.
//
//  Nothing at priority <= 1 may be added to core 1: the spinning loopTask
//  (prio 1) would starve it. buttonPollTask is prio 3, so it preempts fine.
// ===========================================================================
static constexpr uint32_t JOG_REPORT_US = 300; // jog CC cadence (~87% of the UART)
static constexpr uint32_t CTRL_POLL_MS  = 2;   // everything else

void loop() {
    g_loops++; // free-run pass counter, reported as loop-Hz by serialPoll()
    ledPoll(); // first, and outside the DECK_DEBUG branch: drives the LED in both modes

#if DECK_DEBUG
    // Each module self-tests; no MIDI is sent.
    ringA.debug();
    ringB.debug();
    playCue.debug();
    loopBoard.debug();
    jog.debug();
    tempo.debug();
    trackEnc.debug();
    delay(2);
    return;
#endif

    pi.poll(); // incoming MIDI -> LEDs; every pass, for the lowest LED latency

    // ---- jog rotation -> relative CC : every JOG_REPORT_US ----
    // readDelta() drains the PCNT hardware accumulator, so no tick is ever lost
    // between reports -- a longer gap just batches more ticks into the same CC.
    // jog.poll() is deliberately NOT called here: it is touch-only, and its
    // debounce counts samples, so polling it at kHz would shrink the touch
    // window from ~6ms to ~1ms. It stays on the CTRL_POLL_MS gate below.
    static uint32_t lastJogUs = 0;
    const uint32_t  nowUs     = micros();
    if ((uint32_t)(nowUs - lastJogUs) >= JOG_REPORT_US) { // unsigned: rollover-safe
        lastJogUs  = nowUs;
        int32_t jd = jog.readDelta();
        while (jd != 0) { // send full delta in <=63 chunks
            int32_t chunk = jd;
            if (chunk > 63) chunk = 63;
            if (chunk < -63) chunk = -63;
            pi.cc(midimap::CC_JOG, (uint8_t)(chunk & 0x7F)); // 7-bit two's complement
            jd -= chunk;
        }
    }

    // ---- everything below is human-scale: every CTRL_POLL_MS ----
    static uint32_t lastCtrlMs = 0;
    const uint32_t  nowMs      = millis();
    if ((uint32_t)(nowMs - lastCtrlMs) < CTRL_POLL_MS) return; // unsigned: rollover-safe
    lastCtrlMs = nowMs;

    // ---- ring pads -> MIDI notes (both rings) ----
    ringPadsToMidi(ringA, midimap::PAD_A_BASE, RING_A_NODES, padPendingOff);
    ringPadsToMidi(ringB, midimap::PAD_B_BASE, RING_B_NODES, padPendingOffB);

    // ---- jog touch -> scratch enable note (sample-count debounce: keep at 2ms) ----
    jog.poll();
    if (jog.touchPressed()) pi.noteOn(midimap::NOTE_JOG_TOUCH, 127);
    if (jog.touchReleased()) pi.noteOff(midimap::NOTE_JOG_TOUCH);

    // ---- tempo fader -> 14-bit absolute CC (MSB + LSB, only on change) ----
    // Polled by tempoPollTask on core 0; here we only consume the latched value.
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

    // ---- play/cue + loop buttons -> MIDI (presses latched by the poll task:
    //      never missed; Note-Off follows the held level). LEDs come back via onMidi.
    static bool pcPend[PlayCueBoard::COUNT] = {};
    btnToNote(playCue.pressed(PlayCueBoard::PLAY), playCue.level(PlayCueBoard::PLAY),
              midimap::NOTE_PLAY, pcPend[PlayCueBoard::PLAY]);
    btnToNote(playCue.pressed(PlayCueBoard::CUE), playCue.level(PlayCueBoard::CUE),
              midimap::NOTE_CUE, pcPend[PlayCueBoard::CUE]);

    static bool lpPend[LoopBoard::COUNT] = {};
    btnToNote(loopBoard.pressed(LoopBoard::LOOP_START), loopBoard.level(LoopBoard::LOOP_START),
              midimap::NOTE_LOOP_IN, lpPend[LoopBoard::LOOP_START]);
    btnToNote(loopBoard.pressed(LoopBoard::LOOP_END), loopBoard.level(LoopBoard::LOOP_END),
              midimap::NOTE_LOOP_OUT, lpPend[LoopBoard::LOOP_END]);
    btnToNote(loopBoard.pressed(LoopBoard::RELOOP), loopBoard.level(LoopBoard::RELOOP),
              midimap::NOTE_RELOOP, lpPend[LoopBoard::RELOOP]);

    serialPoll(); // low-priority: welcome-on-connect + ~2s stats (self-gated, USB-CDC only)
}
