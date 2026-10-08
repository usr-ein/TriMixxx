#pragma once
// The virtual S3: the deck's controller board, on an emulated Pi's UART (a
// Unix socket QEMU serves). It sends exactly what firmwares/trimixxx-midi
// sends and reads Mixxx's feedback the way lib/PiLink does (s3/protocol), so
// a real S3 on a USB-UART is interchangeable with it.

#include "s3/protocol.h"

#include <QLocalSocket>
#include <QObject>
#include <QTimer>

class VirtualS3 : public QObject {
    Q_OBJECT
public:
    explicit VirtualS3(const QString& socketPath, QObject* parent = nullptr);
    void button(quint8 note, bool down);
    void jog(int ticks);        // signed, 12960 per turn
    void encoder(int detents);  // + = up
    void tempo(int value14);    // 0..16383, 8192 = centre
    void raw(const QByteArray& bytes);
    const DeckLeds& leds() const { return m_feedback.leds(); }
    bool connected() const { return m_sock.state() == QLocalSocket::ConnectedState; }

signals:
    void ledsChanged();
    void linkActivity(bool tx);  // the S3's status LED: green TX, yellow RX
    void connectedChanged(bool);
    void rebooted();             // SysEx RST: every light off, held controls forgotten

private:
    void tryConnect();
    void send(const QByteArray& b);

    QString      m_path;
    QLocalSocket m_sock;
    QTimer       m_retry, m_jogTimer;
    int          m_jogPending = 0;
    s3::Feedback m_feedback;
};
