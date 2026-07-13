#include "LoopBoard.hpp"

// Pins baked into the PCB (verified on the bench: buttons + LEDs both correct).
namespace {
constexpr uint8_t BTN_PIN[] = {1, 10, 5}; // LOOP_START, LOOP_END, RELOOP
constexpr int8_t  LED_PIN[] = {2, 4, -1}; // sink, active-low; RELOOP has no LED
} // namespace

// ISR: latch the press. A single volatile store; pressed() does the read-clear
// under a critical section, so no lock is needed here.
void IRAM_ATTR LoopBoard::isr(void* arg) {
    IsrArg* a                = static_cast<IsrArg*>(arg);
    a->self->_sticky[a->btn] = true;
}

void LoopBoard::begin() {
    for (int b = 0; b < COUNT; b++) {
        pinMode(BTN_PIN[b], INPUT_PULLUP);
        if (LED_PIN[b] >= 0) {
            pinMode(LED_PIN[b], OUTPUT);
            digitalWrite(LED_PIN[b], HIGH); // off (active-low sink)
        }

        _isrArg[b] = {this, (uint8_t)b};
        // FALLING = press (active-low). Attaching from setup() (core 1) installs
        // the GPIO ISR on core 1 -- the opposite core from the ring (core 0).
        attachInterruptArg(digitalPinToInterrupt(BTN_PIN[b]), isr, &_isrArg[b], FALLING);
    }
}

bool LoopBoard::level(Btn b) const {
    return digitalRead(BTN_PIN[b]) == LOW; // active-low, HW-debounced
}

bool LoopBoard::pressed(Btn b) {
    portENTER_CRITICAL(&_mux); // atomic read-and-clear vs the ISR (same core)
    const bool e = _sticky[b];
    _sticky[b]   = false;
    portEXIT_CRITICAL(&_mux);
    return e;
}

void LoopBoard::setLed(Btn b, bool on) {
    if (LED_PIN[b] < 0) return;                // RELOOP has no LED
    digitalWrite(LED_PIN[b], on ? LOW : HIGH); // sink: LOW = on
}
