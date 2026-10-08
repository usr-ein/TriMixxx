#include "board/controlserver.h"

#include "board/link.h"
#include "board/machine.h"
#include "board/sticks.h"
#include "s3/controls.h"
#include "s3/virtuals3.h"
#include "s3/wiring.h"
#include "util/fail.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QPointer>
#include <QTimer>

QString ControlServer::help() {
    return "Deck controls (as the S3 sends them, on this deck's wiring):\n" + s3::controlsHelp() +
           QStringLiteral("\n"
                          "  leds                   what the deck's lights show now (JSON)\n"
                          "USB sticks (two slots, read-only):\n"
                          "  stick list | stick insert ID | stick unplug ID\n"
                          "      ID: diskN, an image file, a folder, or a .stick file describing one\n"
                          "  stick speed RATE ID    how fast it reads: 4M (bytes/s), 4M/300 (and reads/s), full\n"
                          "  stick reads ID | stick reads-reset ID   what the Pi read off a folder stick, by file\n"
                          "Its eth0, the CDJ port:\n"
                          "  link                   its link (the network it shares with other decks), and\n"
                          "                         the player it announces there\n"
                          "  link plug | link unplug   its cable in or out\n"
                          "The board:\n"
                          "  status | power on | power off (pulls the plug) | screenshot FILE.png\n"
                          "  save FILE      the whole machine to FILE, then off (run --restore FILE CARD)\n");
}

ControlServer::ControlServer(Machine* m, VirtualS3* s3, Sticks* sticks, const Wiring* wiring, QObject* parent)
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

bool ControlServer::listen(const QString& path, QString* error) {
    QLocalServer::removeServer(path);
    if (!m_server.listen(path)) { *error = m_server.errorString(); return false; }
    return true;
}

void ControlServer::reply(QLocalSocket* c, bool ok, const QString& text) {
    if (!c || c->state() != QLocalSocket::ConnectedState) return;
    c->write(QJsonDocument(QJsonObject{{"ok", ok}, {"text", text}}).toJson(QJsonDocument::Compact) + "\n");
    c->flush();
}

void ControlServer::onLine(QLocalSocket* client, const QString& line) {
    QPointer<QLocalSocket> c(client);
    QStringList w = line.split(' ', Qt::SkipEmptyParts);
    if (w.isEmpty()) return;
    const QString cmd = w.takeFirst();
    // The S3's controls: the bytes it would send, played on the UART.
    std::optional<s3::Sequence> seq;
    try {
        seq = s3::sequence(QStringList{cmd} + w, *m_wiring);
    } catch (const Failure& f) {
        reply(c, false, f.message);
        return;
    }
    if (seq) { play(c, *seq, 0); return; }

    if (cmd == "help") {
        reply(c, true, help());
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
            for (const auto& s : m_sticks->list()) {
                const QString speed = m_sticks->speedOf(s.id);
                out << QString("%1  %2  %3 MB  %4%5").arg(s.id, -8).arg(s.name, -28)
                           .arg(s.size / 1000000).arg(s.inserted ? "INSERTED" : "-")
                           .arg(speed.isEmpty() ? QString() : "  (" + speed + ")");
            }
            reply(c, true, out.isEmpty() ? "no USB storage on this computer" : out.join('\n'));
        } else if (w[0] == "speed" && w.size() > 2) {
            StickSpeed speed;
            if (!StickSpeed::parse(w[1], &speed)) { reply(c, false, w[1] + ": not a speed (4M, 3.7M/300, full)"); return; }
            m_sticks->setSpeed(line.section(' ', 3, -1, QString::SectionSkipEmpty), speed,
                               [this, c](bool ok, const QString& msg) { reply(c, ok, msg); });
        } else if ((w[0] == "reads" || w[0] == "reads-reset") && w.size() > 1) {
            reply(c, true, m_sticks->reads(line.section(' ', 2, -1, QString::SectionSkipEmpty), w[0] == "reads-reset"));
        } else if ((w[0] == "insert" || w[0] == "unplug") && w.size() > 1) {
            auto done = [this, c](bool ok, const QString& msg) { reply(c, ok, msg); };
            // The rest of the line, spaces and all: a folder's name may have some.
            const QString id = line.section(' ', 2, -1, QString::SectionSkipEmpty);
            if (w[0] == "insert") m_sticks->insert(id, done); else m_sticks->unplug(id, done);
        } else {
            reply(c, false, "stick list | insert ID | unplug ID | speed RATE ID | reads ID | reads-reset ID");
        }
    } else if (cmd == "status") {
        const Link* link = m_machine->options().link;
        reply(c, true, QString("pi: %1\ns3 link: %2\ndeck wiring: %3\neth0: %4\nssh: ssh -p %5 sam1902@127.0.0.1\nrun dir: %6")
                           .arg(m_machine->running() ? "running" : "off")
                           .arg(m_s3->connected() ? "connected" : "waiting for the Pi")
                           .arg(m_wiring->deck())
                           .arg(link ? QString("on link %1 as %2%3").arg(link->net(), link->member(),
                                                                       link->plugged() ? "" : ", unplugged")
                                     : QString("on no link"))
                           .arg(m_machine->options().sshPort)
                           .arg(m_machine->options().runDir));
    } else if (cmd == "link") {
        Link* link = m_machine->options().link;
        if (w.isEmpty()) {
            reply(c, true, link ? linkStatus(*link) : QString("link: none (eth0 is on a cable to an empty switch)"));
        } else if (!link) {
            reply(c, false, "eth0 is on no link: run --link NET (deck up --link NET)");
        } else if (w[0] == "plug" || w[0] == "unplug") {
            // The cable: frames stop at the switch, and the Pi's PHY loses its
            // link (QEMU's set_link), as when a DJ pulls it out.
            const bool in = w[0] == "plug";
            if (in) m_machine->qmp("set_link", QJsonObject{{"name", "link"}, {"up", true}});
            link->setPlugged(in);
            if (!in) m_machine->qmp("set_link", QJsonObject{{"name", "link"}, {"up", false}});
            reply(c, true, QString("%1: eth0 %2 link %3").arg(link->member(), in ? "plugged into" : "unplugged from", link->net()));
        } else {
            reply(c, false, "link | link plug | link unplug");
        }
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
    } else if (cmd == "save" && !w.isEmpty()) {
        // A restore must find the same devices: a stick plugged in now would
        // be missing from the machine that loads this.
        for (const auto& st : m_sticks->list())
            if (st.inserted) { reply(c, false, "unplug the USB sticks first: a saved machine has none"); return; }
        m_machine->save(w[0], [this, c](bool ok, const QString& msg) { reply(c, ok, msg); });
    } else {
        reply(c, false, "unknown command; send help for the list");
    }
}

// One "key: value" a line, as `status`; `player:` is what deck up waits for.
QString ControlServer::linkStatus(const Link& link) {
    QStringList others;
    for (const links::Member& m : links::members(link.net()))
        if (!m.listener && m.name != link.member()) others << m.name + (m.live ? "" : " (gone)");
    const Link::Heard h = link.heard();
    return QString("link: %1\nmember: %2, %3\nplayer: %4\nothers: %5\nframes: %6 out, %7 in, %8 dropped")
        .arg(link.net(), link.member(), link.plugged() ? "plugged in" : "unplugged")
        .arg(h.number ? QString("%1 at %2, %3").arg(h.number).arg(h.ip, h.mac) : QString("none heard"))
        .arg(others.isEmpty() ? QString("none") : others.join(' '))
        .arg(link.framesOut()).arg(link.framesIn()).arg(link.dropped());
}

void ControlServer::play(QLocalSocket* client, const s3::Sequence& s, int from) {
    QPointer<QLocalSocket> c(client);
    for (int i = from; i < s.steps.size(); i++) {
        m_s3->raw(s.steps[i].bytes);
        if (s.steps[i].holdMs > 0 && i + 1 < s.steps.size()) {
            QTimer::singleShot(s.steps[i].holdMs, this, [this, c, s, i] { play(c, s, i + 1); });
            return;
        }
    }
    reply(c, true, s.said);
}
