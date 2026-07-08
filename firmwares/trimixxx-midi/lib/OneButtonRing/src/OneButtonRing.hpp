#pragma once
#include <Arduino.h>

// ===========================================================================
//  OneButtonRing  -  master-side driver for ONE OneButton UART ring.
//
//  One instance == one ring. It owns a FreeRTOS task that continuously
//  circulates a DATA frame (LED bytes out, button bytes back) and exposes a
//  thread-safe snapshot API so the rest of the firmware can read buttons and
//  write LED colours without touching the timing-sensitive ring loop.
//
//  Instantiate it once per ring (e.g. ringA on UART1, ringB on UART2). The
//  class knows nothing about MIDI / Mixxx / the Pi -- it is purely the
//  OneButton Protocol v1.0.0 master:
//      DATA frame = [0xA5][SEQ][ N * 7-byte slots ][CRC8]
//      slot       = R0 G0 B0 R1 G1 B1 BTN   (LEDs out, BTN in)
//      ENUM frame = [0x5A][hopcount]
//      CRC-8 poly 0x07, init 0x00, over TYPE + SEQ + every LED byte.
// ===========================================================================
class OneButtonRing {
public:
    // uart   : a HardwareSerial dedicated to this ring (not shared with anything)
    // txPin  : S3 TX -> node0 DIN   (through the 3.3V->5V level shifter)
    // rxPin  : nodeN DOUT -> S3 RX  (through the 5V->3.3V level shifter)
    // core   : which CPU core to pin the ring task to (0 keeps it off the
    //          Arduino loop, which runs on core 1)
    OneButtonRing(HardwareSerial& uart, int txPin, int rxPin,
                  uint8_t nodeCount, uint32_t baud = 1000000, int core = 0);
    ~OneButtonRing();

    // Configure the UART, allocate buffers, start the ring task, enumerate.
    // Returns false only on allocation failure.
    bool begin();

    // ---- LED output (firmware -> ring). Applied on the next frame. ----
    void setLed(uint8_t node, uint8_t ledIndex, uint8_t r, uint8_t g, uint8_t b);
    void clearLeds();

    // ---- Button input (ring -> firmware) ----
    bool level(uint8_t node);     // true while held (latest frame)
    bool pressed(uint8_t node);   // true if a press happened since last call; CONSUMES it

    // ---- Health / status ----
    uint8_t  configuredNodes() const { return _n; }
    uint8_t  enumeratedNodes() const { return _enumCount; }
    bool     linkOk()          const { return _linkOk; }
    uint32_t goodFrames()      const { return _good; }
    uint32_t badFrames()       const { return _bad; }
    void     reenumerate()           { _reenumReq = true; }

private:
    static void taskTrampoline(void* arg);
    void        taskLoop();
    bool        doEnumerate();
    void        buildFrame();

    static constexpr uint8_t TYPE_DATA   = 0xA5;
    static constexpr uint8_t TYPE_ENUM   = 0x5A;
    static constexpr uint8_t HDR         = 2;     // TYPE + SEQ
    static constexpr uint8_t SLOT        = 7;     // 6 LED + 1 BTN
    static constexpr uint8_t BTN_LEVEL   = 0x01;
    static constexpr uint8_t BTN_STICKY  = 0x02;
    static constexpr uint8_t FAIL_REENUM = 10;    // re-enum after N bad frames

    HardwareSerial& _uart;
    int      _txPin, _rxPin;
    uint8_t  _n;
    uint32_t _baud;
    int      _core;
    size_t   _frameLen = 0;               // HDR + n*SLOT + 1

    uint8_t* _tx       = nullptr;         // outgoing frame
    uint8_t* _rx       = nullptr;         // returned echo
    uint8_t* _led      = nullptr;         // n*6 RGB, guarded by _mux
    uint8_t* _level    = nullptr;         // n, guarded
    uint8_t* _pressAcc = nullptr;         // n accumulated sticky, guarded

    volatile uint8_t  _enumCount = 0;
    volatile bool     _linkOk    = false;
    volatile bool     _reenumReq = false;
    volatile uint32_t _good = 0, _bad = 0;
    uint8_t  _seq = 0;

    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    TaskHandle_t _task = nullptr;
};