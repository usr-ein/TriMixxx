#include "PlayCueBoard.hpp"

// Pins baked into the PCB (verified on the bench: buttons + LEDs both correct).
namespace {
constexpr uint8_t BTN_PIN[] = {21, 35}; // PLAY, CUE
constexpr uint8_t LED_PIN[] = {18, 36}; // PLAY, CUE -- MOSFET, active-high
constexpr uint8_t DEBOUNCE  = 3;        // consecutive stable samples to accept an edge
} // namespace

void PlayCueBoard::begin() {
    for (int b = 0; b < COUNT; b++) {
        pinMode(BTN_PIN[b], INPUT_PULLUP);
        pinMode(LED_PIN[b], OUTPUT);
        digitalWrite(LED_PIN[b], LOW); // off (active-high)
    }
}

void PlayCueBoard::poll() {
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

bool PlayCueBoard::level(Btn b) const { return _level[b]; }

bool PlayCueBoard::pressed(Btn b) {
    taskENTER_CRITICAL(&_mux); // atomic read-and-clear vs the poll task
    const bool e = _sticky[b];
    _sticky[b]   = false;
    taskEXIT_CRITICAL(&_mux);
    return e;
}

void PlayCueBoard::setLed(Btn b, bool on) {
    digitalWrite(LED_PIN[b], on ? HIGH : LOW); // MOSFET: HIGH = on
}

// Self-test: flash both LEDs; a button's LED goes solid while it is held. Reads
// level() (kept fresh by the poll task), so no sampling is done here.
void PlayCueBoard::debug() {
    static uint32_t t     = 0;
    static bool     phase = false;
    if (millis() - t >= 400) {
        t     = millis();
        phase = !phase;
    }
    for (int b = 0; b < COUNT; b++) setLed((Btn)b, level((Btn)b) ? true : phase);
}
