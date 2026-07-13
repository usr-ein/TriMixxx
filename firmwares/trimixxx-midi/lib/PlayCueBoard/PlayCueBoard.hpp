#pragma once
#include <Arduino.h>

// ===========================================================================
//  PlayCueBoard  -  the deck's play/cue board: 2 buttons + 2 LEDs, wired
//  directly to the S3. Pins are baked in (fixed on the PCB), so the ctor takes
//  no config.
//
//  Presses are EDGE-LATCHED via a GPIO interrupt, exactly like the OneButton
//  ring's STICKY bit: the ISR sets a sticky flag the instant the button goes
//  down, so even a tap far shorter than the loop period is never missed. The
//  API mirrors OneButtonRing: level() = current held state, pressed() = "a press
//  happened since you last asked" (consumes the latch).
//
//  ISR CORE: the GPIO ISR service installs on the core that first calls begin().
//  begin() is called from setup(), which runs on the Arduino core (core 1) --
//  deliberately the OPPOSITE core from the ring task (core 0), so the two never
//  contend. Nothing else in this firmware uses a GPIO interrupt.
//
//  Buttons: active-low, internal pull-up + on-board 100nF RC + the input's
//  Schmitt trigger debounce in hardware, so the ISR does pure edge latching (no
//  software debounce), same as the ring node. LEDs: MOSFET gate drive,
//  active-HIGH. MIDI-agnostic: main maps the buttons to notes / drives the LEDs.
// ===========================================================================
class PlayCueBoard {
public:
    enum Btn { PLAY, CUE, COUNT };

    void begin();
    bool level(Btn b) const; // true while held (active-low, HW-debounced)
    bool pressed(Btn b);     // press since last call; latched in the ISR, CONSUMES
    void setLed(Btn b, bool on);

private:
    struct IsrArg {
        PlayCueBoard* self;
        uint8_t       btn;
    };
    static void IRAM_ATTR isr(void* arg);

    IsrArg        _isrArg[COUNT] = {};
    volatile bool _sticky[COUNT] = {};
    portMUX_TYPE  _mux           = portMUX_INITIALIZER_UNLOCKED;
};
