#pragma once
#include <Arduino.h>
#include <ESP32Encoder.h>

// ===========================================================================
//  JogWheel  -  quadrature jog decode via hardware PCNT + a touch input.
//
//  Encoder: GP1A038RBK OPIC optical sensor. JOG1/JOG2 (ch A/B, 90 deg apart)
//  are an actively-driven, comparator-cleaned digital output -- they present a
//  valid logic level at all times (~3.0V high / 0.4V low on 3.3V). Read as
//  plain inputs (no pull by default -- see ctor) and count ALL FOUR quadrature
//  edges in the ESP32-S3 PCNT peripheral (via ESP32Encoder). PCNT is hardware:
//  nothing is missed even at the sensor's ~20 kHz f_max, and it costs zero CPU
//  -- the right answer for fast scratch, no ISR needed.
//
//  Touch: separate platter-touch sense, ACTIVE-LOW (normally HIGH via its own
//  on-board 10k pull-up + 100nF cap, pulled LOW when touched). Read as a
//  floating, lightly-debounced digital input.
//
//  NOTE: this OPTICAL encoder is RAW edge-counted -- its comparator output is
//  already clean, so no debounce. A MECHANICAL contact encoder (e.g. the
//  KY-040 track encoder) is the opposite case and DOES need debouncing --
//  handle that in its own module, not here.
// ===========================================================================
class JogWheel {
public:
    // encoderPullup : weak internal pull-up on JOG1/JOG2. OFF by default (the
    //                 OPIC drives them, nothing to pull). Turn ON only as
    //                 fault-insurance so the pins don't float into phantom
    //                 counts if the jog cable is unplugged. NEVER a pull-down
    //                 -- it would divide against the sensor's high side.
    JogWheel(int pinA, int pinB, int pinTouch, bool encoderPullup = false);
    void begin();

    // Full-quad (x4) ticks per one physical revolution of the platter.
    // Bench-calibrated: 259201 ticks over 20 turns = 12960.05/turn, i.e. exactly
    // 12960 (3240-line disk x4). Sign follows the A/B cable order.
    static constexpr int32_t TICKS_PER_REV = 12960;

    int32_t readDelta(); // signed ticks since last call (consumes)

    void poll(); // call each loop: debounce touch, latch edges
    bool touched() const { return _touchStable; }
    bool touchPressed();  // rising edge (consumes)
    bool touchReleased(); // falling edge (consumes)

    void debug(); // self-test: poll + print movement / touch edges over Serial

private:
    int          _pinA, _pinB, _pinTouch;
    bool         _encoderPullup;
    ESP32Encoder _enc;
    int64_t      _last = 0;

    bool    _touchStable  = false;
    uint8_t _touchCnt     = 0;
    bool    _pressedEdge  = false;
    bool    _releasedEdge = false;
};