#include "control.h"

#include "machine.h"
#include "s3.h"
#include "sticks.h"
#include "wiring.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QPointer>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

QString Control::help() {
    return QStringLiteral(
        "Deck controls (as the S3 sends them, on this deck's wiring):\n"
        "  press CONTROL [MS]     press and release (default 80 ms)\n"
        "  down CONTROL | up CONTROL\n"
        "  jog TICKS              turn the platter; + is clockwise, 12960 ticks per turn\n"
        "  touch on|off           the platter's touch sensor (scratch)\n"
        "  browse N               the track encoder; + is up\n"
        "  tempo VALUE            the fader: 0..16383, 8192 or 'center' in the middle\n"
        "  midi HEX...            raw bytes on the UART, e.g. midi 90 3C 7F\n"
        "  leds                   what the deck's lights show now (JSON)\n"
        "USB sticks (two slots, read-only):\n"
        "  stick list | stick insert ID | stick unplug ID   (ID: diskN or an image file)\n"
        "The board:\n"
        "  status | power on | power off (pulls the plug) | screenshot FILE.png\n"
        "Controls: ") + Wiring::controls().join(' ');
}

Control::Control(Machine* m, S3* s3, Sticks* sticks, const Wiring* wiring, QObject* parent)
    : QObject(parent), m_machine(m), m_s3(s3), m_sticks(sticks), m_wiring(wiring) {
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket* c = m_server.nextPendingConnection()) {
            connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
            connect(c, &QLocalSocket::readyRead, this, [this, c] {
                while (c->canReadLine()) onLine(c, QString::fromUtf8(c->readLine()).trimmed());
            });
        }
    });
}

bool Control::listen(const QString& path, QString* error) {
    QLocalServer::removeServer(path);
    if (!m_server.listen(path)) { *error = m_server.errorString(); return false; }
    QDir().mkpath(QDir::home().filePath(".pi-qemu"));
    QString link = QDir::home().filePath(".pi-qemu/current");
    QFile::remove(link);
    QFile::link(path, link);
    return true;
}

void Control::reply(QLocalSocket* c, bool ok, const QString& text) {
    if (!c || c->state() != QLocalSocket::ConnectedState) return;
    c->write(QJsonDocument(QJsonObject{{"ok", ok}, {"text", text}}).toJson(QJsonDocument::Compact) + "\n");
    c->flush();
}

void Control::onLine(QLocalSocket* client, const QString& line) {
    QPointer<QLocalSocket> c(client);
    QStringList w = line.split(' ', Qt::SkipEmptyParts);
    if (w.isEmpty()) return;
    const QString cmd = w.takeFirst();
    auto note = [&](const QString& name) -> std::optional<quint8> {
        auto n = m_wiring->note(name);
        if (!n) reply(c, false, "no control called " + name + "; try: " + Wiring::controls().join(' '));
        return n;
    };

    if (cmd == "help") {
        reply(c, true, help());
    } else if (cmd == "press" && !w.isEmpty()) {
        auto n = note(w[0]);
        if (!n) return;
        int ms = w.size() > 1 ? w[1].remove("ms").toInt() : 80;
        m_s3->button(*n, true);
        QTimer::singleShot(qMax(ms, 1), this, [this, c, n, ms, name = w[0]] {
            m_s3->button(*n, false);
            reply(c, true, QString("pressed %1 for %2 ms").arg(name).arg(ms));
        });
    } else if ((cmd == "down" || cmd == "up") && !w.isEmpty()) {
        if (auto n = note(w[0])) { m_s3->button(*n, cmd == "down"); reply(c, true, cmd + " " + w[0]); }
    } else if (cmd == "jog" && !w.isEmpty()) {
        int t = w[0].toInt();
        m_s3->jog(m_wiring->jogReversed() ? -t : t); // as this deck's encoder is wired
        reply(c, true, QString("jog %1").arg(t));
    } else if (cmd == "touch" && !w.isEmpty()) {
        if (auto n = note("jog-touch")) { m_s3->button(*n, w[0] == "on"); reply(c, true, "touch " + w[0]); }
    } else if (cmd == "browse" && !w.isEmpty()) {
        m_s3->encoder(w[0].toInt());
        reply(c, true, "browse " + w[0]);
    } else if (cmd == "tempo" && !w.isEmpty()) {
        int v = w[0] == "center" ? 8192 : w[0].toInt();
        m_s3->tempo(v);
        reply(c, true, QString("tempo %1").arg(v));
    } else if (cmd == "midi" && !w.isEmpty()) {
        QByteArray b;
        for (const auto& h : w) b += char(h.toInt(nullptr, 16));
        m_s3->raw(b);
        reply(c, true, QString("sent %1 bytes").arg(b.size()));
    } else if (cmd == "leds") {
        const DeckLeds& l = m_s3->leds();
        auto ring = [&](const auto& r, int n) {
            QJsonArray a;
            for (int i = 0; i < n; i++)
                a.append(QJsonArray{r[i][0].isValid() ? r[i][0].name() : "#000000",
                                    r[i][1].isValid() ? r[i][1].name() : "#000000"});
            return a;
        };
        QJsonObject o{{"play", l.play}, {"cue", l.cue}, {"loopIn", l.loopIn}, {"loopOut", l.loopOut},
                      {"ringA", ring(l.ringA, m_wiring->ringSize(0))},
                      {"ringB", ring(l.ringB, m_wiring->ringSize(1))}};
        reply(c, true, QJsonDocument(o).toJson(QJsonDocument::Compact));
    } else if (cmd == "stick" && !w.isEmpty()) {
        if (w[0] == "list") {
            QStringList out;
            for (const auto& s : m_sticks->list())
                out << QString("%1  %2  %3 MB  %4").arg(s.id, -8).arg(s.name, -28)
                           .arg(s.size / 1000000).arg(s.inserted ? "INSERTED" : "-");
            reply(c, true, out.isEmpty() ? "no USB storage on this computer" : out.join('\n'));
        } else if ((w[0] == "insert" || w[0] == "unplug") && w.size() > 1) {
            auto done = [this, c](bool ok, const QString& msg) { reply(c, ok, msg); };
            if (w[0] == "insert") m_sticks->insert(w[1], done); else m_sticks->unplug(w[1], done);
        } else {
            reply(c, false, "stick list | stick insert ID | stick unplug ID");
        }
    } else if (cmd == "status") {
        reply(c, true, QString("pi: %1\ns3 link: %2\ndeck wiring: %3")
                           .arg(m_machine->running() ? "running" : "off")
                           .arg(m_s3->connected() ? "connected" : "waiting for the Pi")
                           .arg(m_wiring->deck()));
    } else if (cmd == "power" && !w.isEmpty()) {
        if (w[0] == "off") { m_machine->pullPlug(); reply(c, true, "plug pulled"); }
        else if (w[0] == "on") { emit powerOnRequested(); reply(c, true, "powering on"); }
        else reply(c, false, "power on|off");
    } else if (cmd == "screenshot" && !w.isEmpty()) {
        QString file = QFileInfo(w[0]).absoluteFilePath();
        m_machine->qmp("screendump", QJsonObject{{"filename", file}, {"format", "png"}},
                       [this, c, file](const QJsonObject& r) {
                           reply(c, !r.contains("error"),
                                 r.contains("error") ? r["error"].toObject()["desc"].toString() : file);
                       });
    } else {
        reply(c, false, "unknown command; pi-qemu help");
    }
}

QString currentControlSocket() {
    QString env = qEnvironmentVariable("PI_QEMU_CONTROL");
    if (!env.isEmpty()) return env;
    return QFileInfo(QDir::home().filePath(".pi-qemu/current")).symLinkTarget();
}

int runClientCommand(const QString& socket, const QStringList& words) {
    QLocalSocket s;
    s.connectToServer(socket);
    if (socket.isEmpty() || !s.waitForConnected(2000)) {
        fprintf(stderr, "pi-qemu: no running deck (start one with: pi-qemu run CARD.img)\n");
        return 1;
    }
    s.write(words.join(' ').toUtf8() + "\n");
    s.flush();
    // Long enough for a long press or a stick's authorisation prompt.
    QByteArray line;
    while (!line.endsWith('\n') && s.waitForReadyRead(120000)) line += s.readAll();
    QJsonObject r = QJsonDocument::fromJson(line).object();
    if (r.isEmpty()) { fprintf(stderr, "pi-qemu: no answer\n"); return 1; }
    QTextStream(r["ok"].toBool() ? stdout : stderr) << r["text"].toString() << "\n";
    return r["ok"].toBool() ? 0 : 1;
}
