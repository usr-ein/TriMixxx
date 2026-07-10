#include "TempoFader.hpp"

namespace {
constexpr int     ADC_BITS    = 12;    // 0..4095
constexpr int     OVERSAMPLE  = 16;    // per-channel reads averaged each poll
constexpr float   EMA_ALPHA   = 0.20f; // smoothing; lower = calmer, laggier
constexpr int     DEADBAND    = 40;    // ADC counts around center that read 64
constexpr float   HYSTERESIS  = 0.60f; // 7-bit units the value must move to commit
constexpr uint8_t MIDI_CENTER = 64;
constexpr uint8_t MIDI_MAX    = 127;
} // namespace

TempoFader::TempoFader(int pinCenter, int pinWiper, int span, bool invert)
    : _pinCT(pinCenter), _pinWiper(pinWiper), _span(span), _invert(invert) {}

void TempoFader::begin() {
    analogReadResolution(ADC_BITS);
    // Bare 3.3V pot, no external RC on the pins: take the full-scale (~11 dB /
    // ~0..3.1 V) window and lean on oversampling + EMA to clean the noise.
    analogSetPinAttenuation(_pinCT, ADC_11db);
    analogSetPinAttenuation(_pinWiper, ADC_11db);
}

uint16_t TempoFader::readAvg(int pin) const {
    uint32_t acc = 0;
    for (int i = 0; i < OVERSAMPLE; ++i) acc += analogRead(pin);
    return static_cast<uint16_t>(acc / OVERSAMPLE);
}

void TempoFader::poll() {
    const uint16_t ct = readAvg(_pinCT);
    const uint16_t in = readAvg(_pinWiper);

    if (!_primed) { // seed the filters so we don't ramp from 0
        _ctF    = ct;
        _inF    = in;
        _primed = true;
    } else {
        _ctF += (ct - _ctF) * EMA_ALPHA;
        _inF += (in - _inF) * EMA_ALPHA;
    }

    // Ratiometric: everything is relative to the live center tap.
    float delta = _inF - _ctF;                  // signed ADC counts off center
    if (fabsf(delta) <= DEADBAND) delta = 0.0f; // lock the detent to exactly 64
    if (_invert) delta = -delta;

    float v = MIDI_CENTER + (delta * MIDI_CENTER) / _span;
    v       = constrain(v, 0.0f, static_cast<float>(MIDI_MAX));

    // Commit only past the hysteresis band so a value sitting on a code
    // boundary doesn't chatter between two adjacent MIDI numbers.
    if (fabsf(v - _value) >= HYSTERESIS) {
        _value = static_cast<uint8_t>(lroundf(v));
        _dirty = true;
    }
}

bool TempoFader::changed() {
    const bool c = _dirty;
    _dirty       = false;
    return c;
}
