#include "TrackEncoder.hpp"

// Ben Buxton's full-step quadrature table. State cursor lives in the low nibble;
// a completed detent ORs in DIR_CW / DIR_CCW. Invalid (bounce) transitions fall
// back toward R_START without emitting -- that is the debounce.
namespace {
    constexpr uint8_t R_START     = 0x0;
    constexpr uint8_t R_CW_FINAL  = 0x1;
    constexpr uint8_t R_CW_BEGIN  = 0x2;
    constexpr uint8_t R_CW_NEXT   = 0x3;
    constexpr uint8_t R_CCW_BEGIN = 0x4;
    constexpr uint8_t R_CCW_FINAL = 0x5;
    constexpr uint8_t R_CCW_NEXT  = 0x6;

    constexpr uint8_t DIR_CW  = 0x10;
    constexpr uint8_t DIR_CCW = 0x20;

    // Indexed by [state][(DT << 1) | CLK].
    constexpr uint8_t TTABLE[7][4] = {
        {R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START},               // R_START
        {R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW},      // R_CW_FINAL
        {R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START},               // R_CW_BEGIN
        {R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START},               // R_CW_NEXT
        {R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START},               // R_CCW_BEGIN
        {R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | DIR_CCW},     // R_CCW_FINAL
        {R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START},               // R_CCW_NEXT
    };

    constexpr uint8_t SW_DEBOUNCE = 3;   // stable poll samples to accept a switch edge
}

TrackEncoder::TrackEncoder(int pinClk, int pinDt, int pinSw, bool invert)
    : _pinClk(pinClk), _pinDt(pinDt), _pinSw(pinSw), _invert(invert) {}

void TrackEncoder::begin() {
    // Common is GND -> idle HIGH, pulled LOW through the contacts. Internal
    // pull-ups + the external 100 nF caps form the RC pre-filter.
    pinMode(_pinClk, INPUT_PULLUP);
    pinMode(_pinDt,  INPUT_PULLUP);
    pinMode(_pinSw,  INPUT_PULLUP);
    _state = R_START;
}

void TrackEncoder::poll() {
    // ---- rotation ----
    const uint8_t pins = (digitalRead(_pinDt) << 1) | digitalRead(_pinClk);
    _state = TTABLE[_state & 0x0F][pins];
    switch (_state & 0x30) {
        case DIR_CW:  _accum += _invert ? -1 : +1; break;
        case DIR_CCW: _accum += _invert ? +1 : -1; break;
        default: break;
    }

    // ---- switch (active-low, debounced) ----
    const bool raw = (digitalRead(_pinSw) == LOW);
    if (raw != _swStable) {
        if (++_swCnt >= SW_DEBOUNCE) {
            _swStable = raw;
            _swCnt = 0;
            if (raw) _pressEdge = true; else _releaseEdge = true;
        }
    } else {
        _swCnt = 0;
    }
}

int8_t TrackEncoder::readDelta() {
    const int8_t d = _accum;
    _accum = 0;
    return d;
}

bool TrackEncoder::switchPressed()  { const bool e = _pressEdge;   _pressEdge   = false; return e; }
bool TrackEncoder::switchReleased() { const bool e = _releaseEdge; _releaseEdge = false; return e; }
