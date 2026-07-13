#pragma once
#include <Arduino.h>

// ===========================================================================
//  LoopBoard  -  the deck's loop board: 3 buttons + 2 LEDs, wired directly to
//  the S3. Pins are baked in (fixed on the PCB).
//
//  Buttons are sampled + DEBOUNCED in poll(), which a periodic ~2 ms task in
//  main() calls (pinned to core 1, off the ring's core 0). A debounced press
//  sets a sticky latch, so pressed() never misses a real press even if the main
//  loop stalls -- the STICKY idea from the OneButton ring, but POLLED rather than
//  interrupt-driven (robust for switches per Ganssle; avoids the ESP32 edge-IRQ
//  quirks on slow RC-debounced edges).
//
//  level() = debounced held state; pressed() = press-since-last-call (CONSUMES).
//  Buttons active-low (pull-up + 100nF HW debounce). LEDs sink-driven
//  (3V3 -> R -> LED -> GPIO) -> active-LOW. RELOOP has a button but NO LED;
//  setLed(RELOOP, ...) is a no-op. MIDI-agnostic.
// ===========================================================================
class LoopBoard {
public:
    enum Btn { LOOP_START, LOOP_END, RELOOP, COUNT };

    void begin();
    void poll();                 // sample + debounce + latch; call from a ~1-5 ms task
    bool level(Btn b) const;     // debounced held state
    bool pressed(Btn b);         // press since last call; latched, CONSUMES
    void setLed(Btn b, bool on); // no-op for RELOOP (no LED)

    void debug(); // self-test: flash loop LEDs; solid while held; RELOOP lights both

private:
    volatile bool _level[COUNT]  = {}; // debounced held (poll writes, others read)
    volatile bool _sticky[COUNT] = {}; // press latch (poll sets, pressed() clears)
    uint8_t       _cnt[COUNT]    = {}; // debounce counter (poll-private)
    portMUX_TYPE  _mux           = portMUX_INITIALIZER_UNLOCKED;
};
