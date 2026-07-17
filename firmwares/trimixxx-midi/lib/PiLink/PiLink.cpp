#include "PiLink.hpp"

PiLink::PiLink(HardwareSerial& uart, int txPin, int rxPin, uint32_t baud)
    : _uart(uart), _txPin(txPin), _rxPin(rxPin), _baud(baud) {}

void PiLink::begin() { _uart.begin(_baud, SERIAL_8N1, _rxPin, _txPin); }

void PiLink::sendRaw(uint8_t status, uint8_t d1, uint8_t d2) {
    uint8_t m[3] = {status, (uint8_t)(d1 & 0x7F), (uint8_t)(d2 & 0x7F)};
    _uart.write(m, 3);
    _txAct = true; // every send funnels through here -> see tookTx()
}
void PiLink::noteOn(uint8_t n, uint8_t v, uint8_t ch) { sendRaw(0x90 | (ch & 0x0F), n, v); }
void PiLink::noteOff(uint8_t n, uint8_t v, uint8_t ch) { sendRaw(0x80 | (ch & 0x0F), n, v); }
void PiLink::cc(uint8_t c, uint8_t v, uint8_t ch) { sendRaw(0xB0 | (ch & 0x0F), c, v); }

// A complete SysEx arrived: hand the payload over, unless it overran the buffer
// (in which case the whole message is dropped -- never a truncated one).
void PiLink::endSysEx() {
    if (!_sysexOverrun && _sysexHandler && _sysexLen) _sysexHandler(_sysex, _sysexLen, _sysexCtx);
    _inSysEx      = false;
    _sysexOverrun = false;
    _sysexLen     = 0;
}

void PiLink::handleStatus(uint8_t b) {
    // ---- System Real-Time (F8..FF): may interleave ANYWHERE, even mid-message
    // or mid-SysEx. Nothing here uses clock/transport, so drop them -- but do
    // NOT touch the parser state, or a stray clock byte would corrupt whatever
    // message is in flight. (FF is System Reset in raw MIDI, but ttymidi reserves
    // it for its own comment escape and never forwards a reset, so ignore it too.)
    if (b >= 0xF8) return;

    // ---- Any other status byte terminates a SysEx in progress. F7 ends it
    // normally; anything else is an abort -> discard the partial message.
    if (_inSysEx) {
        if (b == 0xF7) {
            endSysEx();
            return;
        }
        _inSysEx      = false;
        _sysexOverrun = false;
        _sysexLen     = 0;
    }

    if (b == 0xF0) { // SysEx start
        _inSysEx      = true;
        _sysexOverrun = false;
        _sysexLen     = 0;
        _status       = 0; // running status does not survive a SysEx
        return;
    }

    // ---- System Common (F1..F7): ttymidi doesn't forward these. Ignore, and
    // cancel running status as the spec requires (a stray F7 lands here too).
    if (b >= 0xF0) {
        _status = 0;
        return;
    }

    // ---- Channel voice ----
    _status    = b;
    uint8_t hi = b & 0xF0;
    _needed    = (hi == 0xC0 || hi == 0xD0) ? 1 : 2; // ProgChange/ChanPressure are 1-data
    _count     = 0;
}

void PiLink::handleData(uint8_t b) {
    if (_inSysEx) {
        if (_sysexLen >= MAX_SYSEX) { // too long: drop the WHOLE message
            if (!_sysexOverrun) {
                _sysexOverrun = true;
                _sysexOverflows++;
            }
            return;
        }
        _sysex[_sysexLen++] = b;
        return;
    }

    if (!_status) return; // data with no status (or after one we ignored)

    if (_count == 0) {
        _d1    = b;
        _count = 1;
        if (_needed == 1) { // 1-data-byte message complete
            if (_handler) _handler(_status, _d1, 0, _ctx);
            _count = 0; // keep _status (running status)
        }
        return;
    }
    if (_handler) _handler(_status, _d1, b, _ctx);
    _count = 0; // keep _status (running status)
}

void PiLink::poll() {
    while (_uart.available()) {
        uint8_t b = _uart.read();
        _rxAct    = true; // every received byte passes here -> see tookRx()
        if (b & 0x80) handleStatus(b);
        else handleData(b);
    }
}
