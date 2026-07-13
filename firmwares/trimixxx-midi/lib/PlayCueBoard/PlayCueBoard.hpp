#pragma once
#include <Arduino.h>

// ===========================================================================
//  PlayCueBoard  -  the deck's play/cue board: 2 buttons + 2 LEDs, wired
//  directly to the S3. Pins are baked in (fixed on the PCB).
//
//  Buttons are sampled + DEBOUNCED in poll(), which a periodic ~2 ms task in
//  main() calls (pinned to core 1, off the ring's core 0). A debounced press
//  sets a sticky latch, so pressed() never misses a real press even if the main
//  loop stalls -- the same STICKY idea as the OneButton ring, but POLLED, not
//  interrupt-driven. Polling is deliberate: it's the robust way to read switches
//  (Ganssle) and sidesteps the ESP32 edge-interrupt quirks that slow RC-debounced
//  edges can provoke.
//
//  level() = debounced held state; pressed() = press-since-last-call (CONSUMES).
//  Buttons active-low (internal pull-up + on-board 100nF RC debounce). LEDs
//  MOSFET-driven, active-HIGH. MIDI-agnostic.
// ===========================================================================
class PlayCueBoard {
public:
    enum Btn { PLAY, CUE, COUNT };

    void begin();
    void poll();             // sample + debounce + latch; call from a ~1-5 ms task
    bool level(Btn b) const; // debounced held state
    bool pressed(Btn b);     // press since last call; latched, CONSUMES
    void setLed(Btn b, bool on);

    void debug(); // self-test: flash LEDs; a button's LED goes solid while held

private:
    volatile bool _level[COUNT]  = {}; // debounced held (poll writes, others read)
    volatile bool _sticky[COUNT] = {}; // press latch (poll sets, pressed() clears)
    uint8_t       _cnt[COUNT]    = {}; // debounce counter (poll-private)
    portMUX_TYPE  _mux           = portMUX_INITIALIZER_UNLOCKED;
};
