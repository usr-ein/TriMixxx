#pragma once
// The virtual S3: the deck's controller board, on the emulated Pi's UART.
//
// Sends exactly what firmwares/trimixxx-midi/src/main.cpp sends, and reads
// Mixxx's feedback the way lib/PiLink does, into the LED state a real deck
// would show. Byte-compatible: a real S3 on a USB-UART is interchangeable.

#include <QColor>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>
#include <array>

struct DeckLeds {
    // Two RGB LEDs per ring node, as the OneButton boards carry.
    std::array<std::array<QColor, 2>, 50> ringA, ringB;
    bool play = false, cue = false, loopIn = false, loopOut = false;
};

class S3 : public QObject {
    Q_OBJECT
public:
    explicit S3(const QString& socketPath, QObject* parent = nullptr);

    void button(quint8 note, bool down);           // Note-On 127 / Note-Off
    void jog(int ticks);                            // signed, 12960 per turn
    void encoder(int detents);                      // + = up
    void tempo(int value14);                        // 0..16383, 8192 = centre
    void raw(const QByteArray& bytes);              // anything else
    const DeckLeds& leds() const { return m_leds; }
    bool connected() const { return m_sock.state() == QLocalSocket::ConnectedState; }

signals:
    void ledsChanged();
    void linkActivity(bool tx);  // the S3's status LED: green TX, yellow RX
    void connectedChanged(bool);
    void rebooted();             // SysEx RST: every light off, held controls forgotten

private:
    void tryConnect();
    void send(const QByteArray& b);
    void onBytes();
    void handleMessage(quint8 status, quint8 d1, quint8 d2);
    void handleSysEx(const QByteArray& payload);

    QString      m_path;
    QLocalSocket m_sock;
    QTimer       m_retry, m_jogTimer;
    int          m_jogPending = 0;
    DeckLeds     m_leds;
    // PiLink's parser state
    quint8     m_status = 0, m_d1 = 0, m_needed = 0, m_count = 0;
    bool       m_inSysEx = false;
    QByteArray m_sysex;
};
