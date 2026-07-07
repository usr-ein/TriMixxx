#include "JogWheel.hpp"

JogWheel::JogWheel(int pinA, int pinB, int pinTouch, bool encoderPullup)
    : _pinA(pinA), _pinB(pinB), _pinTouch(pinTouch), _encoderPullup(encoderPullup) {}

void JogWheel::begin() {
    // JOG1/JOG2: plain digital inputs, NO pull by default. The GP1A038RBK OPIC
    // gives an actively-driven, comparator-cleaned digital output that holds a
    // valid level at all times, so there is nothing to pull.
    //   * NEVER pull down: the sensor's high side is a weak internal ~10k
    //     pull-up; an S3 pull-down would divide against it and sag the high.
    //   * Pull-UP is optional fault-insurance only (parallels the internal 10k,
    //     harmless) -- stops the pins floating into phantom counts if the jog
    //     cable is unplugged. Enabled only if encoderPullup was requested.
    // Numeric puType (0 = none, 2 = pull-up) dodges enum-name churn across
    // ESP32Encoder versions.
    ESP32Encoder::useInternalWeakPullResistors = _encoderPullup ? (puType)2 : (puType)0;

    _enc.attachFullQuad(_pinA, _pinB);     // count all 4 edges in hardware PCNT
    _enc.clearCount();
    _last = 0;

    // Touch: FLOATING digital input. The board conditions this pin (on-board
    // 10k pull-up + 100nF), so add NO internal pull -- it would parallel the
    // external pull-up and shorten the debounce RC.
    pinMode(_pinTouch, INPUT);
}

int32_t JogWheel::readDelta() {
    int64_t now = _enc.getCount();         // hardware-accumulated quadrature count
    int32_t d = (int32_t)(now - _last);
    _last = now;
    return d;
}

void JogWheel::poll() {
    // Touch only. The quadrature is counted in hardware and needs no polling
    // and no debounce (the OPIC comparator output is already clean).
    // Active-low: touched == LOW, otherwise HIGH.
    bool raw = (digitalRead(_pinTouch) == LOW);
    if (raw != _touchStable) {
        if (++_touchCnt >= 3) {            // light debounce for the touch contact
            _touchStable = raw;
            _touchCnt = 0;
            if (raw) _pressedEdge = true; else _releasedEdge = true;
        }
    } else {
        _touchCnt = 0;
    }
}

bool JogWheel::touchPressed()  { bool e = _pressedEdge;  _pressedEdge  = false; return e; }
bool JogWheel::touchReleased() { bool e = _releasedEdge; _releasedEdge = false; return e; }