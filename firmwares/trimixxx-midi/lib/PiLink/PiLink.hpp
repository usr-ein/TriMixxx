#pragma once
#include <Arduino.h>

// ===========================================================================
//  PiLink  -  bidirectional raw-MIDI-over-UART link to the Raspberry Pi.
//
//  ttymidi on the Pi injects these bytes into ALSA, so Mixxx sees a standard
//  MIDI device. Generic: knows nothing about the deck. Send helpers push MIDI
//  out; poll() parses incoming MIDI (with running-status support) and hands
//  each complete message to a callback -- used to drive LEDs from Mixxx.
//
//  The Pi link is 3.3 V both ends (S3 + Pi GPIO), so NO level shifter here.
// ===========================================================================
class PiLink {
public:
    // status, data1, data2, user-context
    using MidiHandler = void (*)(uint8_t status, uint8_t d1, uint8_t d2, void* ctx);

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
    void poll(); // call often; parses RX, fires the handler

private:
    HardwareSerial& _uart;
    int             _txPin, _rxPin;
    uint32_t        _baud;

    MidiHandler _handler = nullptr;
    void*       _ctx     = nullptr;

    // MIDI parser state (running status)
    uint8_t _status = 0;
    uint8_t _d1     = 0;
    uint8_t _needed = 0;
    uint8_t _count  = 0;
};