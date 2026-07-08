#include "PiLink.hpp"

PiLink::PiLink(HardwareSerial& uart, int txPin, int rxPin, uint32_t baud)
    : _uart(uart), _txPin(txPin), _rxPin(rxPin), _baud(baud) {}

void PiLink::begin() {
    _uart.begin(_baud, SERIAL_8N1, _rxPin, _txPin);
}

void PiLink::sendRaw(uint8_t status, uint8_t d1, uint8_t d2) {
    uint8_t m[3] = { status, (uint8_t)(d1 & 0x7F), (uint8_t)(d2 & 0x7F) };
    _uart.write(m, 3);
}
void PiLink::noteOn (uint8_t n, uint8_t v, uint8_t ch){ sendRaw(0x90 | (ch & 0x0F), n, v); }
void PiLink::noteOff(uint8_t n, uint8_t v, uint8_t ch){ sendRaw(0x80 | (ch & 0x0F), n, v); }
void PiLink::cc     (uint8_t c, uint8_t v, uint8_t ch){ sendRaw(0xB0 | (ch & 0x0F), c, v); }

// Minimal MIDI parser: channel-voice messages only, running status supported.
void PiLink::poll() {
    while (_uart.available()) {
        uint8_t b = _uart.read();
        if (b & 0x80) {                          // status byte
            if (b >= 0xF0) { _status = 0; continue; }   // system msgs: ignore
            _status = b;
            uint8_t hi = b & 0xF0;
            _needed = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
            _count  = 0;
        } else {                                 // data byte
            if (!_status) continue;              // no status yet
            if (_count == 0) {
                _d1 = b; _count = 1;
                if (_needed == 1) {              // 1-byte message complete
                    if (_handler) _handler(_status, _d1, 0, _ctx);
                    _count = 0;                  // keep _status (running status)
                }
            } else {
                if (_handler) _handler(_status, _d1, b, _ctx);
                _count = 0;                       // keep _status (running status)
            }
        }
    }
}