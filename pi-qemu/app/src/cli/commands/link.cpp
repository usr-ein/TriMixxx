// pi-qemu link: the networks emulated decks' eth0s share (board/link.h), seen
// from the Mac -- who is on one, who announces what, and every frame.

#include "cli/cli.h"
#include "cli/commands/commands.h"

#include "board/link.h"
#include "util/fail.h"
#include "util/print.h"
#include "util/process.h"
#include "util/tool.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <chrono>
#include <map>
#include <vector>

namespace {

int list(cli::Args& a) {
    a.done();
    bool any = false;
    for (const QString& net : links::nets()) {
        QStringList names;
        for (const links::Member& m : links::members(net))
            if (!m.listener) names << (m.live ? m.name : m.name + " (gone)");
        if (names.isEmpty()) continue;
        any = true;
        print() << net << ": " << names.join(' ') << "\n";
    }
    if (!any) print() << "no links: " << tool("deck up NAME --link NET") << " puts a deck on one\n";
    return 0;
}

LinkTap openTap(const QString& net) {
    if (!links::validName(net)) fail("a link's name is letters, digits, - and _: not " + net, 2);
    if (!QFileInfo::exists(links::dir(net))) fail("no link " + net + " (" + tool("link list") + ")");
    return LinkTap(net);
}

// Every Pro DJ Link device that announces itself on NET, as a player there
// sees them: its keep-alives (UDP 50000), which go out every 1.5 to 2 s.
int devices(cli::Args& a) {
    const QString net = a.take("NET");
    const int seconds = a.takeNumber("SECONDS", 4);
    a.done();
    LinkTap tap = openTap(net);
    QString err;
    if (!tap.open(&err)) fail(err);
    struct Seen {
        links::KeepAlive k;
        QString from;
    };
    std::map<QString, Seen> seen; // by the MAC it announces
    std::vector<char> frame;
    QString from;
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < seconds * 1000LL && !proc::interrupted()) {
        if (!tap.next(&frame, &from, 100)) continue;
        const links::KeepAlive k = links::keepAlive(frame.data(), frame.size());
        if (k.number) seen[k.mac] = Seen{k, from};
    }
    if (seen.empty()) {
        print() << "nothing announced itself on " << net << " in " << seconds << " s\n";
        return 0;
    }
    std::vector<Seen> rows;
    for (const auto& [mac, s] : seen) rows.push_back(s);
    std::sort(rows.begin(), rows.end(), [](const Seen& x, const Seen& y) { return x.k.number < y.k.number; });
    print() << "player  name                  address           MAC                deck\n";
    for (const Seen& s : rows)
        print() << QString::number(s.k.number).leftJustified(7) << " " << s.k.name.leftJustified(21) << " "
                << s.k.ip.leftJustified(17) << " " << s.k.mac.leftJustified(18) << " " << s.from << "\n";
    return 0;
}

void put32(QByteArray& b, quint32 v) {
    const char le[4] = {char(v), char(v >> 8), char(v >> 16), char(v >> 24)};
    b.append(le, 4);
}

// Every frame on NET into a pcap file (Ethernet), for Wireshark or
// `prolink pcap`. Each frame is there once: a deck sends it to the link once.
int capture(cli::Args& a) {
    const QString net = a.take("NET");
    const QString file = QFileInfo(a.take("FILE.pcap")).absoluteFilePath();
    const int seconds = a.takeNumber("SECONDS", 0);
    a.done();
    LinkTap tap = openTap(net);
    QString err;
    if (!tap.open(&err)) fail(err);
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) fail("cannot write " + file);
    QByteArray head;
    for (quint32 v : {0xa1b2c3d4u, 0x00040002u, 0u, 0u, 65535u, 1u}) put32(head, v); // v2.4, Ethernet
    out.write(head);
    out.flush();
    print() << "capturing link " << net << " into " << file
            << (seconds ? QString(" for %1 s").arg(seconds) : QString(" until ^C")) << "\n";
    std::vector<char> frame;
    quint64 frames = 0;
    QElapsedTimer t;
    t.start();
    while ((!seconds || t.elapsed() < seconds * 1000LL) && !proc::interrupted()) {
        if (!tap.next(&frame, nullptr, 200)) continue;
        const qint64 now = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::system_clock::now().time_since_epoch()).count();
        QByteArray rec;
        put32(rec, quint32(now / 1000000));
        put32(rec, quint32(now % 1000000));
        put32(rec, quint32(frame.size()));
        put32(rec, quint32(frame.size()));
        rec.append(frame.data(), qsizetype(frame.size()));
        out.write(rec);
        out.flush(); // a valid file at every moment
        frames++;
    }
    print() << frames << " frames in " << file << "\n";
    return 0;
}

} // namespace

namespace commands {

void addLink(cli::Registry& r) {
    r.group("link", "the networks emulated decks share, as players on one switch (deck up --link)");
    r.add({.group = "link", .name = "list", .summary = "every link, and the decks on it", .run = list});
    r.add({
        .group = "link", .name = "devices", .synopsis = "NET [SECONDS]",
        .summary = "the Pro DJ Link players announcing themselves on NET, and their decks",
        .help = "Listens on NET for SECONDS (4) without sending anything, and lists every\n"
                "device whose keep-alives it heard: its player number, name, address and MAC,\n"
                "and the deck that sent them.",
        .run = devices,
    });
    r.add({
        .group = "link", .name = "capture", .synopsis = "NET FILE.pcap [SECONDS]",
        .summary = "every frame on NET into a pcap file, until ^C or for SECONDS",
        .help = "Ethernet frames, each once, as Wireshark (or `prolink pcap`) reads them. The\n"
                "file is valid at every moment. Listening sends nothing.",
        .run = capture,
    });
}

} // namespace commands
