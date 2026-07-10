#pragma once
#include <Arduino.h>

// ===========================================================================
//  TrackEncoder  -  GIAK KY-040 mechanical rotary encoder (track select).
//
//  This is the MECHANICAL counterpart to the optical JogWheel: its contacts
//  bounce, so raw edge counting would emit phantom detents. We decode CLK/DT
//  with Ben Buxton's full-step quadrature state table -- it only emits a step
//  after a complete, valid Gray-code sequence, so bounce that doesn't complete
//  a transition is silently rejected. One tick per physical detent, no delays.
//
//  Wiring (3.3 V): CLK and DT each have a 100 nF cap to GND. The encoder common
//  is GND, so the pins idle HIGH on the internal pull-ups and are pulled LOW
//  through the contacts; cap + ~45k pull-up gives a free RC pre-filter. SW is
//  an active-low push switch to GND, debounced here.
//
//  Human-speed input, so poll() from the main loop is plenty -- no ISR.
// ===========================================================================
class TrackEncoder {
public:
    // invert : flip CW/CCW if "up" turns out to be wired the other way.
    TrackEncoder(int pinClk, int pinDt, int pinSw, bool invert = false);
    void begin();

    void poll();                       // decode rotation + debounce switch

    int8_t readDelta();                // signed detents since last call; CONSUMES

    bool switchLevel() const { return _swStable; }  // true while held
    bool switchPressed();              // falling edge (press); CONSUMES
    bool switchReleased();             // rising edge (release); CONSUMES

private:
    int  _pinClk, _pinDt, _pinSw;
    bool _invert;

    uint8_t _state = 0;                // quadrature state-table cursor
    int8_t  _accum = 0;                // detents pending for readDelta()

    bool    _swStable   = false;       // debounced, true == pressed (active-low)
    uint8_t _swCnt      = 0;
    bool    _pressEdge  = false;
    bool    _releaseEdge = false;
};
