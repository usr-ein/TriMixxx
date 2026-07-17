#pragma once
#include <Arduino.h>

// ===========================================================================
//  PiLink  -  bidirectional raw-MIDI-over-UART link to the Raspberry Pi.
//
//  ttymidi on the Pi injects these bytes into ALSA, so Mixxx sees a standard
//  MIDI device. Generic: knows nothing about the deck. Send helpers push MIDI
//  out; poll() parses incoming MIDI and hands each complete message to a
//  callback -- used to drive LEDs from Mixxx.
//
//  The parser handles the three things that actually arrive on this link:
//    - channel-voice messages, with running status (ttymidi never emits
//      running status itself, but a real MIDI source upstream of it may);
//    - SysEx (F0 .. F7), reassembled into a fixed buffer and handed over whole;
//    - System Real-Time (F8..FF), which may appear ANYWHERE -- including
//      between the data bytes of a message, or inside a SysEx. These are
//      ignored WITHOUT disturbing the message in progress, per the MIDI spec.
//      Mixxx/ALSA can emit clock and active-sensing; dropping running status on
//      them (or feeding them to the SysEx buffer) would corrupt real messages.
//
//  The Pi link is 3.3 V both ends (S3 + Pi GPIO), so NO level shifter here.
// ===========================================================================
class PiLink {
public:
    // status, data1, data2, user-context
    using MidiHandler = void (*)(uint8_t status, uint8_t d1, uint8_t d2, void* ctx);
    // payload = the bytes BETWEEN F0 and F7 (markers stripped), len = payload length
    using SysExHandler = void (*)(const uint8_t* payload, uint8_t len, void* ctx);

    // Largest SysEx payload we accept, markers excluded. The ring-LED command is
    // 15 bytes; this leaves room to grow. Longer messages are dropped WHOLE (the
    // handler never sees a truncated one). Static buffer -- no heap.
    static constexpr uint8_t MAX_SYSEX = 64;

    PiLink(HardwareSerial& uart, int txPin, int rxPin, uint32_t baud = 115200);
    void begin();

    // ---- send (channel is 0-based; default channel 1) ----
    void noteOn(uint8_t note, uint8_t vel, uint8_t ch = 0);
    void noteOff(uint8_t note, uint8_t vel = 0, uint8_t ch = 0);
    void cc(uint8_t control, uint8_t value, uint8_t ch = 0);
    void sendRaw(uint8_t status, uint8_t d1, uint8_t d2);

    // ---- receive ----
    void onMidi(MidiHandler h, void* ctx) {
        _handler = h;
        _ctx     = ctx;
    }
    void onSysEx(SysExHandler h, void* ctx) {
        _sysexHandler = h;
        _sysexCtx     = ctx;
    }
    void poll(); // call often; parses RX, fires the handlers

    // Diagnostics: SysEx messages dropped for overrunning MAX_SYSEX.
    uint32_t sysexOverflows() const { return _sysexOverflows; }

    // Link activity, consume-on-read: true if any byte moved in that direction
    // since the last call, then cleared. Set at the two choke points every byte
    // passes through (sendRaw / poll), so no caller has to report its own
    // traffic. Lets main drive an Ethernet-style TX/RX activity LED without the
    // driver knowing anything about LEDs.
    bool tookTx() {
        const bool a = _txAct;
        _txAct       = false;
        return a;
    }
    bool tookRx() {
        const bool a = _rxAct;
        _rxAct       = false;
        return a;
    }

private:
    void handleStatus(uint8_t b);
    void handleData(uint8_t b);
    void endSysEx();

    HardwareSerial& _uart;
    int             _txPin, _rxPin;
    uint32_t        _baud;

    MidiHandler _handler = nullptr;
    void*       _ctx     = nullptr;

    SysExHandler _sysexHandler = nullptr;
    void*        _sysexCtx     = nullptr;

    // Channel-voice parser state (running status)
    uint8_t _status = 0;
    uint8_t _d1     = 0;
    uint8_t _needed = 0;
    uint8_t _count  = 0;

    // SysEx reassembly state
    bool     _inSysEx          = false;
    bool     _sysexOverrun     = false; // this message already blew the buffer -> drop it
    uint8_t  _sysexLen         = 0;
    uint8_t  _sysex[MAX_SYSEX] = {};
    uint32_t _sysexOverflows   = 0;

    // Link-activity flags (see tookTx/tookRx). Single-context: set in sendRaw()
    // and poll(), both called only from loop(), so no volatile or lock needed.
    bool _txAct = false;
    bool _rxAct = false;
};
