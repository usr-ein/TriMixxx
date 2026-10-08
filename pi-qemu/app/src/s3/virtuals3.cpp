#include "s3/virtuals3.h"

VirtualS3::VirtualS3(const QString& socketPath, QObject* parent) : QObject(parent), m_path(socketPath) {
    m_feedback.onLeds = [this] { emit ledsChanged(); };
    m_feedback.onReboot = [this] { emit rebooted(); };
    connect(&m_sock, &QLocalSocket::readyRead, this, [this] {
        const QByteArray in = m_sock.readAll();
        if (!in.isEmpty()) emit linkActivity(false);
        m_feedback.feed(in);
    });
    connect(&m_sock, &QLocalSocket::connected, this, [this] { emit connectedChanged(true); });
    connect(&m_sock, &QLocalSocket::disconnected, this, [this] {
        emit connectedChanged(false);
        m_retry.start();
    });
    // QEMU's socket appears when the board starts and goes when it stops (and
    // on every reboot): keep reconnecting, as the S3's UART simply stays wired.
    m_retry.setInterval(300);
    connect(&m_retry, &QTimer::timeout, this, &VirtualS3::tryConnect);
    m_retry.start();

    // Jog rotation goes out on its own cadence, like main.cpp's JOG_REPORT_US:
    // ticks accumulate and leave together.
    m_jogTimer.setInterval(2);
    m_jogTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_jogTimer, &QTimer::timeout, this, [this] {
        if (!m_jogPending) { m_jogTimer.stop(); return; }
        send(s3::jog(m_jogPending));
        m_jogPending = 0;
    });
}

void VirtualS3::tryConnect() {
    if (m_sock.state() != QLocalSocket::UnconnectedState) return;
    m_sock.connectToServer(m_path);
    if (m_sock.waitForConnected(50)) m_retry.stop();
}

void VirtualS3::send(const QByteArray& b) {
    if (!connected()) return; // a UART with nobody listening: the bytes are gone
    m_sock.write(b);
    m_sock.flush();
    emit linkActivity(true);
}

void VirtualS3::button(quint8 note, bool down) { send(s3::button(note, down)); }
void VirtualS3::encoder(int detents) { send(s3::encoder(detents)); }
void VirtualS3::tempo(int v) { send(s3::tempo(v)); }
void VirtualS3::raw(const QByteArray& bytes) { send(bytes); }

void VirtualS3::jog(int ticks) {
    m_jogPending += ticks;
    if (!m_jogTimer.isActive()) m_jogTimer.start();
}
