#include "LoopBoard.hpp"

// Pins baked into the PCB (verified on the bench: buttons + LEDs both correct).
namespace {
constexpr uint8_t BTN_PIN[] = {1, 10, 5}; // LOOP_START, LOOP_END, RELOOP
constexpr int8_t  LED_PIN[] = {2, 4, -1}; // sink, active-low; RELOOP has no LED
constexpr uint8_t DEBOUNCE  = 3;          // consecutive stable samples to accept an edge
} // namespace

void LoopBoard::begin() {
    for (int b = 0; b < COUNT; b++) {
        pinMode(BTN_PIN[b], INPUT_PULLUP);
        if (LED_PIN[b] >= 0) {
            pinMode(LED_PIN[b], OUTPUT);
            digitalWrite(LED_PIN[b], HIGH); // off (active-low sink)
        }
    }
}

void LoopBoard::poll() {
    for (int b = 0; b < COUNT; b++) {
        const bool raw = (digitalRead(BTN_PIN[b]) == LOW); // active-low
        if (raw == _level[b]) {
            _cnt[b] = 0; // no change; reset the streak
            continue;
        }
        if (++_cnt[b] < DEBOUNCE) continue; // change not yet stable
        _cnt[b] = 0;
        taskENTER_CRITICAL(&_mux);
        _level[b] = raw;
        if (raw) _sticky[b] = true; // latch the press edge
        taskEXIT_CRITICAL(&_mux);
    }
}

bool LoopBoard::level(Btn b) const { return _level[b]; }

bool LoopBoard::pressed(Btn b) {
    taskENTER_CRITICAL(&_mux); // atomic read-and-clear vs the poll task
    const bool e = _sticky[b];
    _sticky[b]   = false;
    taskEXIT_CRITICAL(&_mux);
    return e;
}

void LoopBoard::setLed(Btn b, bool on) {
    if (LED_PIN[b] < 0) return;                // RELOOP has no LED
    digitalWrite(LED_PIN[b], on ? LOW : HIGH); // sink: LOW = on
}

// Self-test: flash both loop LEDs; a loop button's LED goes solid while held,
// and RELOOP (no LED of its own) lights BOTH loop LEDs solid while held. Reads
// level() (kept fresh by the poll task), so no sampling is done here.
void LoopBoard::debug() {
    static uint32_t t     = 0;
    static bool     phase = false;
    if (millis() - t >= 400) {
        t     = millis();
        phase = !phase;
    }
    const bool reloop = level(RELOOP);
    setLed(LOOP_START, (level(LOOP_START) || reloop) ? true : phase);
    setLed(LOOP_END, (level(LOOP_END) || reloop) ? true : phase);
}
