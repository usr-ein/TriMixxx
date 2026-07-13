#pragma once
#include <Arduino.h>

// ===========================================================================
//  LoopBoard  -  the deck's loop board: 3 buttons + 2 LEDs, wired directly to
//  the S3. Pins are baked in (fixed on the PCB), so the ctor takes no config.
//
//  Presses are EDGE-LATCHED via a GPIO interrupt (like the OneButton ring's
//  STICKY bit): the ISR sets a sticky flag the instant the button goes down, so
//  a tap far shorter than the loop period is never missed. API mirrors
//  OneButtonRing: level() = held state, pressed() = press-since-last-call
//  (consumes the latch).
//
//  ISR CORE: attached in begin() (called from setup(), the Arduino core = core
//  1), deliberately the OPPOSITE core from the ring task (core 0).
//
//  Buttons: active-low, internal pull-up + on-board 100nF RC + Schmitt debounce
//  in hardware -> pure edge latching, no software debounce. LEDs: sink-driven
//  (3V3 -> R -> LED -> GPIO) -> active-LOW. RELOOP has a button but NO LED;
//  setLed(RELOOP, ...) is a no-op. MIDI-agnostic.
// ===========================================================================
class LoopBoard {
public:
    enum Btn { LOOP_START, LOOP_END, RELOOP, COUNT };

    void begin();
    bool level(Btn b) const;     // true while held (active-low, HW-debounced)
    bool pressed(Btn b);         // press since last call; latched in the ISR, CONSUMES
    void setLed(Btn b, bool on); // no-op for RELOOP (no LED)

private:
    struct IsrArg {
        LoopBoard* self;
        uint8_t    btn;
    };
    static void IRAM_ATTR isr(void* arg);

    IsrArg        _isrArg[COUNT] = {};
    volatile bool _sticky[COUNT] = {};
    portMUX_TYPE  _mux           = portMUX_INITIALIZER_UNLOCKED;
};
