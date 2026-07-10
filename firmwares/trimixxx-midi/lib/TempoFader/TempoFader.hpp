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
//  the exact center-detent voltage all cancel, so center always maps to MIDI
//  64 without calibration. Deflection either side scales to 0..127.
//
//  Both pins must be on ADC1 (S3: GPIO1..10) -- ADC2 is unusable with WiFi up.
//  IO8/IO9 are ADC1, good. 12-bit reads, oversampled + EMA-smoothed; a small
//  center dead-band pins the detent firmly to 64.
// ===========================================================================
class TempoFader {
public:
    // pinCenter : ADCT, the center-tap reference.
    // pinWiper  : ADIN, the moving wiper.
    // span      : ADC counts from center to full deflection (half-throw). The
    //             fader hits MIDI 0 / 127 at +/- this. Tune to the real throw.
    // invert    : swap which way is "faster" if the fader is wired upside down.
    TempoFader(int pinCenter, int pinWiper, int span = 1900, bool invert = false);
    void begin();

    void    poll();                          // read + filter; call each loop
    uint8_t value() const { return _value; } // 0..127, 64 == center
    bool    changed();                       // true if value moved since last call; CONSUMES

private:
    uint16_t readAvg(int pin) const; // oversampled single-channel read

    int  _pinCT, _pinWiper;
    int  _span;
    bool _invert;

    float   _ctF    = 0; // EMA-filtered center + wiper (ADC counts)
    float   _inF    = 0;
    bool    _primed = false; // first poll seeds the filters
    uint8_t _value  = 64;
    bool    _dirty  = false; // pending change for changed()
};
