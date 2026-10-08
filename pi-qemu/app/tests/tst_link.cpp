// board/link: decks' eth0s on one link. Each test stands in for the Pis with
// the ends QEMU would hold, in a links directory of its own.

#include "board/link.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <memory>

namespace {

using Frame = QByteArray;

// An Ethernet frame from `src` to `dst`, its payload marked so it can be told apart.
Frame frame(const char* dst, const char* src, const QByteArray& payload) {
    auto mac = [](const char* text) {
        QByteArray b;
        for (const QString& octet : QString(text).split(':')) b.append(char(octet.toInt(nullptr, 16)));
        return b;
    };
    return mac(dst) + mac(src) + QByteArray("\x88\xb5", 2) + payload; // a local experimental ethertype
}

constexpr const char* kBroadcast = "ff:ff:ff:ff:ff:ff";
constexpr const char* kMacA = "02:54:4d:00:00:0a";
constexpr const char* kMacB = "02:54:4d:00:00:0b";
constexpr const char* kMacC = "02:54:4d:00:00:0c";

void put(const Link& l, const Frame& f) { QCOMPARE(::send(l.qemuFd(), f.constData(), size_t(f.size()), 0), ssize_t(f.size())); }

// What reaches the Pi behind `l` within `ms`; empty if nothing does.
Frame take(const Link& l, int ms = 1500) {
    pollfd p{l.qemuFd(), POLLIN, 0};
    if (::poll(&p, 1, ms) <= 0) return {};
    char buf[2048];
    const ssize_t n = ::recv(l.qemuFd(), buf, sizeof(buf), 0);
    return n > 0 ? Frame(buf, int(n)) : Frame();
}

Frame tap(LinkTap& t, QString* from = nullptr, int ms = 1500) {
    std::vector<char> f;
    if (!t.next(&f, from, ms)) return {};
    return Frame(f.data(), int(f.size()));
}

// A keep-alive as a deck sends it: Ethernet, IPv4, UDP to 50000, then the
// Pro DJ Link packet (docs/PROTOCOL.md in lib/prolink, section 2).
Frame keepAlive(int number, const QByteArray& ip, const char* mac) {
    QByteArray p(0x36, 0);
    std::memcpy(p.data(), "Qspt1WmJOL", 10);
    p[0x0a] = 0x06;
    std::memcpy(p.data() + 0x0c, "CDJ-2000nexus", 13);
    p[0x20] = 0x01;
    p[0x21] = 0x02;
    p[0x23] = 0x36;
    p[0x24] = char(number);
    p[0x25] = 0x02;
    const Frame macBytes = frame(mac, mac, {}).left(6);
    std::memcpy(p.data() + 0x26, macBytes.constData(), 6);
    std::memcpy(p.data() + 0x2c, ip.constData(), 4);
    p[0x30] = 1;
    QByteArray udp(8, 0);
    udp[0] = char(0xc3), udp[1] = char(0x50); // from 50000
    udp[2] = char(0xc3), udp[3] = char(0x50); // to 50000
    udp[4] = char((8 + p.size()) >> 8), udp[5] = char(8 + p.size());
    QByteArray ipv4(20, 0);
    ipv4[0] = 0x45;
    ipv4[2] = char((20 + udp.size() + p.size()) >> 8), ipv4[3] = char(20 + udp.size() + p.size());
    ipv4[8] = 64, ipv4[9] = 17;
    ipv4.replace(12, 4, ip);
    ipv4.replace(16, 4, QByteArray("\xa9\xfe\xff\xff", 4));
    return frame(kBroadcast, mac, {}).left(12) + QByteArray("\x08\x00", 2) + ipv4 + udp + p;
}

// Until every link has scanned its members once more (each looks every second).
void settle() { QThread::msleep(1200); }

} // namespace

class TestLink : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;

private slots:
    void init() {
        QVERIFY(m_dir.isValid());
        // Short: the sockets' paths must stay under macOS's 104 bytes.
        qputenv("PI_QEMU_LINKS", QFile::encodeName(m_dir.path()));
    }

    void aDecksMacIsItsOwnAndStable() {
        const QString a = links::macFor("plink-a");
        QCOMPARE(a, links::macFor("plink-a"));
        QVERIFY(a != links::macFor("plink-b"));
        QVERIFY(a.startsWith("02:54:4d:"));
        QCOMPARE(a.size(), 17);
        QCOMPARE(a.right(2).toInt(nullptr, 16) % 2, 0); // even: the management NIC is the next one up
    }

    void aBroadcastReachesEveryOtherDeckButNeverComesBack() {
        Link a("booth", "a"), b("booth", "b"), c("booth", "c");
        QString error;
        QVERIFY2(a.open(&error), qPrintable(error));
        QVERIFY2(b.open(&error), qPrintable(error));
        QVERIFY2(c.open(&error), qPrintable(error));
        settle();
        const Frame hello = frame(kBroadcast, kMacA, "hello from a");
        put(a, hello);
        QCOMPARE(take(b), hello);
        QCOMPARE(take(c), hello);
        QVERIFY(take(a, 300).isEmpty());
        QCOMPARE(a.framesOut(), quint64(1));
        QCOMPARE(b.framesIn(), quint64(1));
    }

    void aFrameForAKnownMacGoesToItsDeckAlone() {
        Link a("booth", "a"), b("booth", "b"), c("booth", "c");
        QString error;
        QVERIFY(a.open(&error) && b.open(&error) && c.open(&error));
        LinkTap t("booth");
        QVERIFY2(t.open(&error), qPrintable(error));
        settle();
        // a hears from b, and so learns where b's MAC is.
        put(b, frame(kBroadcast, kMacB, "b is here"));
        QCOMPARE(take(a), frame(kBroadcast, kMacB, "b is here"));
        QCOMPARE(take(c), frame(kBroadcast, kMacB, "b is here"));
        QCOMPARE(tap(t), frame(kBroadcast, kMacB, "b is here"));

        const Frame toB = frame(kMacB, kMacA, "for b only");
        put(a, toB);
        QCOMPARE(take(b), toB);
        QVERIFY(take(c, 300).isEmpty());
        QString from;
        QCOMPARE(tap(t, &from), toB); // a listener hears everything...
        QCOMPARE(from, QString("a")); // ...and who sent it

        // A MAC nobody has spoken from yet goes to everyone, as a switch floods it.
        const Frame toC = frame(kMacC, kMacA, "for c, not yet heard from");
        put(a, toC);
        QCOMPARE(take(b), toC);
        QCOMPARE(take(c), toC);
    }

    void anUnpluggedDeckNeitherSendsNorHears() {
        Link a("booth", "a"), b("booth", "b");
        QString error;
        QVERIFY(a.open(&error) && b.open(&error));
        settle();
        a.setPlugged(false);
        put(a, frame(kBroadcast, kMacA, "into the void"));
        QVERIFY(take(b, 300).isEmpty());
        put(b, frame(kBroadcast, kMacB, "unheard"));
        QVERIFY(take(a, 300).isEmpty());
        a.setPlugged(true);
        put(b, frame(kBroadcast, kMacB, "heard"));
        QCOMPARE(take(a), frame(kBroadcast, kMacB, "heard"));
    }

    void aNameTakenOnALinkIsRefusedAndAStaleOneReplaced() {
        QString error;
        auto first = std::make_unique<Link>("booth", "a");
        QVERIFY(first->open(&error));
        Link second("booth", "a");
        QVERIFY(!second.open(&error));
        QVERIFY2(error.contains("already on this link"), qPrintable(error));
        first.reset();

        // A board killed outright leaves its socket file behind.
        const QByteArray path = QFile::encodeName(links::dir("booth") + "/a.sock");
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path, path.constData(), size_t(path.size()));
        const int s = ::socket(AF_UNIX, SOCK_DGRAM, 0);
        QCOMPARE(::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
        ::close(s);
        QVERIFY(QFile::exists(QFile::decodeName(path)));
        const QList<links::Member> left = links::members("booth");
        QCOMPARE(left.size(), 1);
        QVERIFY(!left[0].live);

        Link again("booth", "a");
        QVERIFY2(again.open(&error), qPrintable(error));
        QVERIFY(links::members("booth").value(0).live);
    }

    void namesAreChecked() {
        QString error;
        Link bad("booth", "../x");
        QVERIFY(!bad.open(&error));
        Link badNet("a/b", "x");
        QVERIFY(!badNet.open(&error));
    }

    void aKeepAliveSaysWhoSentIt() {
        const Frame f = keepAlive(3, QByteArray("\xa9\xfe\xed\xf3", 4), kMacB);
        const links::KeepAlive k = links::keepAlive(f.constData(), size_t(f.size()));
        QCOMPARE(k.number, 3);
        QCOMPARE(k.name, QString("CDJ-2000nexus"));
        QCOMPARE(k.ip, QString("169.254.237.243"));
        QCOMPARE(k.mac, QString(kMacB));

        QCOMPARE(links::keepAlive(f.constData(), size_t(f.size() - 10)).number, 0); // cut short
        Frame other = f;
        other[14 + 20 + 8 + 0x0a] = 0x0a; // a hello, not a keep-alive
        QCOMPARE(links::keepAlive(other.constData(), size_t(other.size())).number, 0);
        Frame arp = frame(kBroadcast, kMacB, QByteArray(46, 0));
        arp[12] = 0x08, arp[13] = 0x06;
        QCOMPARE(links::keepAlive(arp.constData(), size_t(arp.size())).number, 0);
    }

    void aBoardKnowsWhatPlayerItAnnounces() {
        Link a("booth", "a");
        QString error;
        QVERIFY(a.open(&error));
        QCOMPARE(a.heard().number, 0);
        put(a, keepAlive(4, QByteArray("\xa9\xfe\xdc\xd8", 4), kMacA));
        for (int i = 0; i < 50 && !a.heard().number; i++) QThread::msleep(20);
        QCOMPARE(a.heard().number, 4);
        QCOMPARE(a.heard().ip, QString("169.254.220.216"));
        // What an unplugged board sends is not announced anywhere.
        a.setPlugged(false);
        a.setPlugged(true);
        QCOMPARE(a.heard().number, 0);
    }

    void framesForAPiThatWasOffAreForgotten() {
        Link a("booth", "a"), b("booth", "b");
        QString error;
        QVERIFY(a.open(&error) && b.open(&error));
        settle();
        put(b, frame(kBroadcast, kMacB, "while a was off"));
        QThread::msleep(200);
        a.drain();
        QVERIFY(take(a, 300).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestLink)
#include "tst_link.moc"
