#include "s3.h"

#include "MidiMap.hpp"

using namespace midimap;

S3::S3(const QString& socketPath, QObject* parent) : QObject(parent), m_path(socketPath) {
    connect(&m_sock, &QLocalSocket::readyRead, this, &S3::onBytes);
    connect(&m_sock, &QLocalSocket::connected, this, [this] { emit connectedChanged(true); });
    connect(&m_sock, &QLocalSocket::disconnected, this, [this] {
        emit connectedChanged(false);
        m_retry.start();
    });
    // QEMU's socket appears when the board starts and goes when it stops (and
    // on every reboot): keep reconnecting, as the S3's UART simply stays wired.
    m_retry.setInterval(300);
    connect(&m_retry, &QTimer::timeout, this, &S3::tryConnect);
    m_retry.start();

    // Jog rotation goes out on its own cadence, like main.cpp's JOG_REPORT_US:
    // ticks accumulate and leave in chunks of at most 63, 7-bit two's complement.
    m_jogTimer.setInterval(2);
    m_jogTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_jogTimer, &QTimer::timeout, this, [this] {
        if (!m_jogPending) { m_jogTimer.stop(); return; }
        QByteArray out;
        while (m_jogPending) {
            int chunk = qBound(-63, m_jogPending, 63);
            out += char(0xB0 | CHANNEL);
            out += char(CC_JOG);
            out += char(chunk & 0x7F);
            m_jogPending -= chunk;
        }
        send(out);
    });
}

void S3::tryConnect() {
    if (m_sock.state() != QLocalSocket::UnconnectedState) return;
    m_sock.connectToServer(m_path);
    if (m_sock.waitForConnected(50)) m_retry.stop();
}

void S3::send(const QByteArray& b) {
    if (!connected()) return; // a UART with nobody listening: the bytes are gone
    m_sock.write(b);
    m_sock.flush();
    emit linkActivity(true);
}

void S3::button(quint8 note, bool down) {
    // One pair per press, nothing while held (btnToNote in main.cpp).
    QByteArray b;
    b += char((down ? 0x90 : 0x80) | CHANNEL);
    b += char(note & 0x7F);
    b += char(down ? 127 : 0);
    send(b);
}

void S3::jog(int ticks) {
    m_jogPending += ticks;
    if (!m_jogTimer.isActive()) m_jogTimer.start();
}

void S3::encoder(int detents) {
    QByteArray b;
    for (int i = 0; i < qAbs(detents); i++) {
        b += char(0xB0 | CHANNEL);
        b += char(CC_ENCODER);
        b += char(detents > 0 ? 1 : 127);
    }
    send(b);
}

void S3::tempo(int v) {
    v = qBound(0, v, 16383);
    QByteArray b;
    b += char(0xB0 | CHANNEL); b += char(CC_TEMPO);     b += char(v >> 7);
    b += char(0xB0 | CHANNEL); b += char(CC_TEMPO_LSB); b += char(v & 0x7F);
    send(b);
}

void S3::raw(const QByteArray& bytes) { send(bytes); }

// ---- feedback from Mixxx: PiLink::poll(), byte for byte ----------------------
void S3::onBytes() {
    const QByteArray in = m_sock.readAll();
    if (!in.isEmpty()) emit linkActivity(false);
    for (char c : in) {
        quint8 b = quint8(c);
        if (b & 0x80) {
            if (b >= 0xF8) continue; // real-time: anywhere, ignored, state untouched
            if (m_inSysEx) {
                m_inSysEx = false;
                if (b == 0xF7) { handleSysEx(m_sysex); continue; }
            }
            if (b == 0xF0) { m_inSysEx = true; m_sysex.clear(); m_status = 0; continue; }
            if (b >= 0xF0) { m_status = 0; continue; }
            m_status = b;
            quint8 hi = b & 0xF0;
            m_needed = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
            m_count = 0;
            continue;
        }
        if (m_inSysEx) {
            if (m_sysex.size() < 64) m_sysex += char(b); // longer: dropped whole at F7
            else m_sysex += char(0x80);                  // poison: never a valid payload
            continue;
        }
        if (!m_status) continue;
        if (m_count == 0) {
            m_d1 = b;
            m_count = 1;
            if (m_needed == 1) { handleMessage(m_status, m_d1, 0); m_count = 0; }
            continue;
        }
        handleMessage(m_status, m_d1, b);
        m_count = 0;
    }
}

void S3::handleMessage(quint8 status, quint8 d1, quint8 d2) {
    quint8 type = status & 0xF0;
    // Note-On velocity on a ring pad = white brightness, LED0 only, on the
    // LEDs' 0..255 scale as main.cpp passes it (so at most half brightness).
    if (type == 0x90 && d1 < PAD_A_BASE + PAD_A_COUNT) {
        m_leds.ringA[d1 - PAD_A_BASE][0] = QColor(d2, d2, d2);
    } else if (type == 0x90 && d1 >= PAD_B_BASE && d1 < PAD_B_BASE + PAD_B_COUNT) {
        m_leds.ringB[d1 - PAD_B_BASE][0] = QColor(d2, d2, d2);
    } else if (type == 0x90 || type == 0x80) {
        bool on = type == 0x90 && d2 > 0;
        switch (d1) {
        case NOTE_PLAY: m_leds.play = on; break;
        case NOTE_CUE: m_leds.cue = on; break;
        case NOTE_LOOP_IN: m_leds.loopIn = on; break;
        case NOTE_LOOP_OUT: m_leds.loopOut = on; break;
        default: return; // RELOOP has no LED
        }
    } else {
        return;
    }
    emit ledsChanged();
}

void S3::handleSysEx(const QByteArray& p) {
    if (p.size() < 2 || quint8(p[0]) != SYSEX_MFR_ID || p.contains(char(0x80))) return;
    const quint8 cmd = quint8(p[1]);
    const QByteArray args = p.mid(2);
    auto nib = [&](int i) { return (quint8(args[i]) << 4) | (quint8(args[i + 1]) & 0x0F); };
    if (cmd == SYSEX_CMD_RING_LED || cmd == SYSEX_CMD_RING_B_LED) {
        const int n = args.size();
        if (n != SYSEX_RING_LED_ARGS_ONE && n != SYSEX_RING_LED_ARGS_TWO) return;
        const int node = quint8(args[0]);
        if (node >= 50) return;
        auto& ring = cmd == SYSEX_CMD_RING_LED ? m_leds.ringA : m_leds.ringB;
        ring[node][0] = QColor(nib(1), nib(3), nib(5));
        ring[node][1] = n == SYSEX_RING_LED_ARGS_ONE ? ring[node][0] : QColor(nib(7), nib(9), nib(11));
        emit ledsChanged();
    } else if (cmd == SYSEX_CMD_RESET && args == QByteArray("RST")) {
        m_leds = DeckLeds{};
        emit ledsChanged();
        emit rebooted();
    }
}
