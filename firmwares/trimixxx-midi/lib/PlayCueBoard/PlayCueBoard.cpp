#include "PlayCueBoard.hpp"

// Pins baked into the PCB (verified on the bench: buttons + LEDs both correct).
namespace {
constexpr uint8_t BTN_PIN[] = {21, 35}; // PLAY, CUE
constexpr uint8_t LED_PIN[] = {18, 36}; // PLAY, CUE -- MOSFET, active-high
} // namespace

// ISR: latch the press. A single volatile store; pressed() does the read-clear
// under a critical section, so no lock is needed here.
void IRAM_ATTR PlayCueBoard::isr(void* arg) {
    IsrArg* a                = static_cast<IsrArg*>(arg);
    a->self->_sticky[a->btn] = true;
}

void PlayCueBoard::begin() {
    for (int b = 0; b < COUNT; b++) {
        pinMode(BTN_PIN[b], INPUT_PULLUP);
        pinMode(LED_PIN[b], OUTPUT);
        digitalWrite(LED_PIN[b], LOW); // off (active-high)

        _isrArg[b] = {this, (uint8_t)b};
        // FALLING = press (active-low). Attaching from setup() (core 1) installs
        // the GPIO ISR on core 1 -- the opposite core from the ring (core 0).
        attachInterruptArg(digitalPinToInterrupt(BTN_PIN[b]), isr, &_isrArg[b], FALLING);
    }
}

bool PlayCueBoard::level(Btn b) const {
    return digitalRead(BTN_PIN[b]) == LOW; // active-low, HW-debounced
}

bool PlayCueBoard::pressed(Btn b) {
    portENTER_CRITICAL(&_mux); // atomic read-and-clear vs the ISR (same core)
    const bool e = _sticky[b];
    _sticky[b]   = false;
    portEXIT_CRITICAL(&_mux);
    return e;
}

void PlayCueBoard::setLed(Btn b, bool on) {
    digitalWrite(LED_PIN[b], on ? HIGH : LOW); // MOSFET: HIGH = on
}
