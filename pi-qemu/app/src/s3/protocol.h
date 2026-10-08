#pragma once
// The S3's MIDI, both ways, as firmwares/trimixxx-midi sends and reads it:
// the bytes each control sends, and the parser for what Mixxx sends back,
// into the lights a deck shows. No I/O here. The virtual S3 puts these bytes
// on an emulated deck's UART; a real deck gets the same bytes straight into
// Mixxx's MIDI port (decks/remotedeck).

#include <QByteArray>
#include <QColor>
#include <QStringList>

#include <array>
#include <functional>

struct DeckLeds {
    // Two RGB LEDs per ring node, as the OneButton boards carry.
    std::array<std::array<QColor, 2>, 50> ringA, ringB;
    bool play = false, cue = false, loopIn = false, loopOut = false;
};

namespace s3 {

QByteArray button(quint8 note, bool down);  // Note-On 127 / Note-Off
QByteArray jog(int ticks);                  // chunks of at most 63, 7-bit two's complement
QByteArray encoder(int detents);            // + is up
QByteArray tempo(int value14);              // 0..16383, 8192 the centre
// "90 3C 7F" as bytes; false if a word isn't a byte in hex.
bool parseHex(const QStringList& words, QByteArray* out);

// PiLink::poll(), byte for byte: Mixxx's feedback into a deck's lights.
class Feedback {
public:
    std::function<void()> onLeds;    // any light changed
    std::function<void()> onReboot;  // SysEx RST: every light off
    void feed(const QByteArray& in);
    const DeckLeds& leds() const { return m_leds; }

private:
    void message(quint8 status, quint8 d1, quint8 d2);
    void sysex(const QByteArray& payload);

    DeckLeds   m_leds;
    quint8     m_status = 0, m_d1 = 0, m_needed = 0, m_count = 0;
    bool       m_inSysEx = false;
    QByteArray m_sysex;
};

} // namespace s3
