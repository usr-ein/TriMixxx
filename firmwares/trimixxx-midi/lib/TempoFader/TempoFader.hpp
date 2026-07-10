#pragma once
#include <Arduino.h>

// ===========================================================================
//  TempoFader  -  ratiometric slide-fader read for the pitch/tempo control.
//
//  Two ADC inputs instead of one:
//    ADCT (center tap) : the electrical MIDDLE of the fader travel -- the
//                        "zero" of the tempo range (no pitch change).
//    ADIN (wiper)      : the fader's current position.
//
//  The value is derived from (ADIN - ADCT), NOT from ADIN alone. Measuring the
//  center live makes the reading RATIOMETRIC: supply sag, ADC gain drift and
//  the exact center-detent voltage all cancel, so center always maps to the
//  midpoint without calibration. Output is 14-bit (0..16383, 8192 = center) so
//  a MIDI 14-bit CC pair carries it -- far finer than a single 7-bit CC.
//
//  Both pins must be on ADC1 (S3: GPIO1..10) -- ADC2 is unusable with WiFi up.
//  IO8/IO9 are ADC1, good. 12-bit reads, oversampled + EMA-smoothed; a small
//  center dead-band pins the detent firmly to 64.
// ===========================================================================
class TempoFader {
public:
    // pinCenter : ADCT, the center-tap reference.
    // pinWiper  : ADIN, the moving wiper.
    // spanToMax : |ADC offset| from center to the extreme that reads 16383.
    // spanToMin : |ADC offset| from center to the extreme that reads 0.
    //             Per-side because this fader's center tap is off electrical
    //             mid -- each direction has a different count of travel. Use the
    //             offset= readout at each stop.
    // invert    : which physical end is which (true = high end reads low).
    TempoFader(int pinCenter, int pinWiper, int spanToMax, int spanToMin, bool invert = false);
    void begin();

    void     poll();                          // read + filter; call each loop
    uint16_t value() const { return _value; } // 0..16383 (14-bit), 8192 == center
    bool     changed();                       // true if value moved since last call; CONSUMES

    // ---- calibration helpers (filtered ADC counts; watch these to pick span) ----
    uint16_t center() const { return (uint16_t)(_ctF + 0.5f); } // ADCT, filtered
    uint16_t wiper() const { return (uint16_t)(_inF + 0.5f); }  // ADIN, filtered
    int      offset() const { return (int)(_inF - _ctF); }      // signed counts off center

private:
    uint16_t readAvg(int pin) const; // oversampled single-channel read

    int  _pinCT, _pinWiper;
    int  _spanMax, _spanMin; // per-side half-spans (ADC counts)
    bool _invert;

    float    _ctF    = 0; // EMA-filtered center + wiper (ADC counts)
    float    _inF    = 0;
    bool     _primed = false; // first poll seeds the filters
    uint16_t _value  = 8192;  // 14-bit center
    bool     _dirty  = false; // pending change for changed()
};
