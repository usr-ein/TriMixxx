#include "OneButtonRing.hpp"

// CRC-8, poly 0x07, init 0x00, MSB-first (OneButton Protocol v1.0.0), folded
// one byte at a time so we can skip the BTN bytes in the coverage.
static inline uint8_t crc8_upd(uint8_t c, uint8_t d) {
    c ^= d;
    for (int i = 0; i < 8; i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
    return c;
}

OneButtonRing::OneButtonRing(HardwareSerial& uart, int txPin, int rxPin, uint8_t nodeCount,
                             uint32_t baud, int core)
    : _uart(uart), _txPin(txPin), _rxPin(rxPin), _n(nodeCount), _baud(baud), _core(core) {}

OneButtonRing::~OneButtonRing() {
    if (_task) vTaskDelete(_task);
    free(_tx);
    free(_rx);
    free(_led);
    free(_level);
    free(_pressAcc);
}

bool OneButtonRing::begin() {
    _frameLen = HDR + (size_t)_n * SLOT + 1;
    _tx       = (uint8_t*)malloc(_frameLen);
    _rx       = (uint8_t*)malloc(_frameLen);
    _led      = (uint8_t*)calloc((size_t)_n * 6, 1);
    _level    = (uint8_t*)calloc(_n, 1);
    _pressAcc = (uint8_t*)calloc(_n, 1);
    if (!_tx || !_rx || !_led || !_level || !_pressAcc) return false;

    // RX buffer must hold a whole returned frame; the echo streams in (via
    // cut-through) while we are still transmitting, so it can't be read yet.
    _uart.setRxBufferSize(_frameLen + 128);
    _uart.begin(_baud, SERIAL_8N1, _rxPin, _txPin);
    _uart.setTimeout(20); // ms; comfortably > ring round-trip

    xTaskCreatePinnedToCore(taskTrampoline, "1btn_ring", 4096, this, 5, &_task, _core);
    return true;
}

void OneButtonRing::taskTrampoline(void* arg) { static_cast<OneButtonRing*>(arg)->taskLoop(); }

// Send ENUM (hop=0); each node adopts its index and forwards hop+1, so the
// returned second byte is the node count. Positional addressing, no config.
bool OneButtonRing::doEnumerate() {
    while (_uart.available()) _uart.read(); // discard stragglers
    uint8_t e[2] = {TYPE_ENUM, 0x00};
    _uart.write(e, 2);
    _uart.flush(); // wait until TX drained
    uint8_t r[2];
    size_t  got = _uart.readBytes(r, 2);
    if (got == 2 && r[0] == TYPE_ENUM) {
        _enumCount = r[1];
        _linkOk    = (_enumCount == _n);
        return _linkOk;
    }
    _linkOk = false;
    return false;
}

// Author a DATA frame: LED bytes from the shared buffer, BTN placeholders 0,
// then the CRC over header + LED bytes only.
void OneButtonRing::buildFrame() {
    _tx[0] = TYPE_DATA;
    _tx[1] = ++_seq;

    taskENTER_CRITICAL(&_mux); // brief: copy LED bytes out
    for (uint8_t n = 0; n < _n; n++) {
        uint8_t*       slot = &_tx[HDR + n * SLOT];
        const uint8_t* src  = &_led[n * 6];
        slot[0]             = src[0];
        slot[1]             = src[1];
        slot[2]             = src[2];
        slot[3]             = src[3];
        slot[4]             = src[4];
        slot[5]             = src[5];
        slot[6]             = 0x00; // BTN placeholder (node fills)
    }
    taskEXIT_CRITICAL(&_mux);

    uint8_t c = 0;
    c         = crc8_upd(c, _tx[0]);
    c         = crc8_upd(c, _tx[1]);
    for (uint8_t n = 0; n < _n; n++) {
        const uint8_t* slot = &_tx[HDR + n * SLOT];
        for (int k = 0; k < 6; k++) c = crc8_upd(c, slot[k]);
    }
    _tx[_frameLen - 1] = c;
}

void OneButtonRing::taskLoop() {
    doEnumerate();
    uint8_t consecFail = 0;

    for (;;) {
        if (_reenumReq) {
            _reenumReq = false;
            doEnumerate();
        }

        buildFrame();
        while (_uart.available()) _uart.read(); // clean RX before this frame
        _uart.write(_tx, _frameLen);            // TX (echo streams back as we go)
        size_t got = _uart.readBytes(_rx, _frameLen);

        bool ok = false;
        if (got == _frameLen && _rx[0] == TYPE_DATA && _rx[1] == _tx[1]) {
            uint8_t c = 0;
            c         = crc8_upd(c, _rx[0]);
            c         = crc8_upd(c, _rx[1]);
            for (uint8_t n = 0; n < _n; n++) {
                const uint8_t* slot = &_rx[HDR + n * SLOT];
                for (int k = 0; k < 6; k++) c = crc8_upd(c, slot[k]);
            }
            ok = (c == _rx[_frameLen - 1]);
        }

        if (ok) {
            taskENTER_CRITICAL(&_mux);
            for (uint8_t n = 0; n < _n; n++) {
                uint8_t b = _rx[HDR + n * SLOT + 6];
                _level[n] = (b & BTN_LEVEL) ? 1 : 0;
                if (b & BTN_STICKY) _pressAcc[n] = 1; // OR-accumulate across frames
            }
            taskEXIT_CRITICAL(&_mux);
            _linkOk = true;
            _good++;
            consecFail = 0;
        } else {
            _linkOk = false;
            _bad++;
            if (++consecFail >= FAIL_REENUM) {
                consecFail = 0;
                doEnumerate();
            }
        }

        // Yield every iteration: prevents the task WDT and lets same-core tasks
        // run. The echo read already blocks ~one round-trip, so this mostly just
        // paces the frame rate (~150-250 Hz at 1 Mbps / 50 nodes).
        vTaskDelay(1);
    }
}

void OneButtonRing::setLed(uint8_t node, uint8_t ledIndex, uint8_t r, uint8_t g, uint8_t b) {
    if (node >= _n || ledIndex > 1) return;
    uint8_t* p = &_led[node * 6 + ledIndex * 3];
    taskENTER_CRITICAL(&_mux);
    p[0] = r;
    p[1] = g;
    p[2] = b;
    taskEXIT_CRITICAL(&_mux);
}

void OneButtonRing::clearLeds() {
    taskENTER_CRITICAL(&_mux);
    memset(_led, 0, (size_t)_n * 6);
    taskEXIT_CRITICAL(&_mux);
}

bool OneButtonRing::level(uint8_t node) {
    if (node >= _n) return false;
    taskENTER_CRITICAL(&_mux);
    bool v = _level[node];
    taskEXIT_CRITICAL(&_mux);
    return v;
}

bool OneButtonRing::pressed(uint8_t node) {
    if (node >= _n) return false;
    taskENTER_CRITICAL(&_mux);
    bool v          = _pressAcc[node];
    _pressAcc[node] = 0; // consume the edge
    taskEXIT_CRITICAL(&_mux);
    return v;
}