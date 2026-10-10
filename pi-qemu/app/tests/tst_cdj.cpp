// cdj/ and board/streamport: an emulated CDJ's MAC, and its NIC's stream on a
// link. Each test stands in for the CDJ's QEMU with a stream client, in a
// links directory of its own.

#include "board/link.h"
#include "board/streamport.h"
#include "cdj/cdj.h"

#include <QFile>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

QByteArray framed(const QByteArray& frame) {
    const std::vector<char> v = StreamFramer::encode(frame.constData(), size_t(frame.size()));
    return QByteArray(v.data(), qsizetype(v.size()));
}

// The CDJ's QEMU: `-netdev stream,server=off,addr.type=unix` connects to the port.
int connectTo(const QString& path) {
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    const QByteArray p = QFile::encodeName(path);
    std::memcpy(a.sun_path, p.constData(), size_t(p.size()));
    const int s = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        ::close(s);
        return -1;
    }
    return s;
}

// Exactly n bytes from a stream within ms; fewer if they do not come.
QByteArray readStream(int fd, int n, int ms = 1500) {
    QByteArray out;
    while (out.size() < n) {
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, ms) <= 0) break;
        char buf[2048];
        const ssize_t got = ::recv(fd, buf, size_t(std::min<qsizetype>(sizeof(buf), n - out.size())), 0);
        if (got <= 0) break;
        out.append(buf, qsizetype(got));
    }
    return out;
}

QByteArray readDatagram(int fd, int ms = 1500) {
    pollfd p{fd, POLLIN, 0};
    if (::poll(&p, 1, ms) <= 0) return {};
    char buf[2048];
    const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    return n > 0 ? QByteArray(buf, qsizetype(n)) : QByteArray();
}

// An ARP reply as Linux writes it: 42 bytes, no padding.
const QByteArray kArp = QByteArray::fromHex("0243440a0b0c02544d000004" "0806") + QByteArray(28, '\xa5');

void settle() { QThread::msleep(300); }

} // namespace

class TestCdj : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;

private slots:
    void init() {
        QVERIFY(m_dir.isValid());
        qputenv("PI_QEMU_LINKS", QFile::encodeName(m_dir.path() + "/links"));
        qputenv("PI_QEMU_CDJ", QFile::encodeName(m_dir.path() + "/cdj"));
    }

    void aCdjsMacIsItsOwnStableAndUsable() {
        const QString a = cdj::macFor("cdj-a");
        QCOMPARE(a, cdj::macFor("cdj-a"));
        QVERIFY(a != cdj::macFor("cdj-b"));
        QVERIFY(a.startsWith("02:43:44:"));
        QVERIFY(a != links::macFor("cdj-a").replace("02:54:4d", "02:43:44"));
        QSet<QString> seen;
        for (int i = 0; i < 2000; i++) {
            const QString m = cdj::macFor(QString("cdj-%1").arg(i));
            QCOMPARE(m.size(), 17);
            // The NXS's link-local address is 169.254.<5th>.<6th>: not 169.254.0/24
            // or 169.254.255/24, which RFC 3927 reserves.
            const int fifth = m.section(':', 4, 4).toInt(nullptr, 16);
            QVERIFY2(fifth >= 1 && fifth <= 254, qPrintable(m));
            seen.insert(m);
        }
        QCOMPARE(seen.size(), 2000);
    }

    void aCdjsOptionsReachTheEmulator() {
        QCOMPARE(cdj::launchOptions(QJsonObject{}), QStringList{});
        const QJsonObject c{{"usb", "/x/stick.img"}, {"dsp_model", true}, {"window", false}};
        QCOMPARE(cdj::launchOptions(c), (QStringList{"--usb", "/x/stick.img", "--dsp-model"}));
        QVERIFY(!cdj::launchOptions(QJsonObject{{"dsp_model", false}}).contains("--dsp-model"));
        QCOMPARE(cdj::launchOptions(QJsonObject{{"sd", "/c.img"}, {"test_track", false}, {"window", true}}),
                 (QStringList{"--sd", "/c.img", "--ui"}));
    }

    void theFramerCutsAStreamIntoFrames() {
        StreamFramer f;
        std::vector<std::string> got;
        auto take = [&](const char* d, size_t n) { got.emplace_back(d, n); };
        const QByteArray stream = framed("first") + framed("second, longer") + framed("x");
        for (char c : stream) QVERIFY(f.feed(&c, 1, take)); // a byte at a time
        QCOMPARE(got.size(), size_t(3));
        QCOMPARE(QByteArray::fromStdString(got[1]), QByteArray("second, longer"));
        got.clear();
        QVERIFY(f.feed(stream.constData(), size_t(stream.size()), take)); // all at once
        QCOMPARE(got.size(), size_t(3));
        StreamFramer broken;
        QVERIFY(!broken.feed("\0\0\0\0", 4, take)); // a zero length
        StreamFramer tooLong;
        QVERIFY(!tooLong.feed("\0\x01\0\0", 4, take)); // 65536 bytes
    }

    void aStreamPortCarriesFramesBothWays() {
        int pair[2];
        QCOMPARE(::socketpair(AF_UNIX, SOCK_DGRAM, 0, pair), 0); // pair[0]: what Link's qemuFd() would be
        StreamPort port(m_dir.path() + "/s.sock", pair[0]);
        QString error;
        QVERIFY2(port.open(&error), qPrintable(error));
        const int qemu = connectTo(m_dir.path() + "/s.sock");
        QVERIFY(qemu >= 0);
        const QByteArray out = framed(kArp);
        QCOMPARE(::send(qemu, out.constData(), size_t(out.size()), 0), ssize_t(out.size()));
        QCOMPARE(readDatagram(pair[1]), kArp); // one frame, one datagram, as sent
        QCOMPARE(::send(pair[1], kArp.constData(), size_t(kArp.size()), 0), ssize_t(kArp.size()));
        QCOMPARE(readStream(qemu, 4 + int(kArp.size())), framed(kArp));
        QVERIFY(port.connected());
        QCOMPARE(port.framesIn(), quint64(1));
        QCOMPARE(port.framesOut(), quint64(1));
        ::close(qemu);
        ::close(pair[1]);
        port.close();
        ::close(pair[0]);
        QVERIFY(!QFile::exists(m_dir.path() + "/s.sock"));
    }

    void aBrokenStreamIsDroppedAndTheNextEmulatorStartsAfresh() {
        int pair[2];
        QCOMPARE(::socketpair(AF_UNIX, SOCK_DGRAM, 0, pair), 0);
        StreamPort port(m_dir.path() + "/s.sock", pair[0]);
        QString error;
        QVERIFY(port.open(&error));
        const int bad = connectTo(m_dir.path() + "/s.sock");
        QCOMPARE(::send(bad, "\0\0\0\0junk", 8, 0), ssize_t(8));
        QCOMPARE(readStream(bad, 1).size(), 0); // closed on it
        ::close(bad);
        // Nobody connected: the link's frames go nowhere, and are not kept.
        QCOMPARE(::send(pair[1], kArp.constData(), size_t(kArp.size()), 0), ssize_t(kArp.size()));
        settle();
        const int good = connectTo(m_dir.path() + "/s.sock");
        QVERIFY(good >= 0);
        QVERIFY(readStream(good, 1, 400).isEmpty());
        const QByteArray out = framed("after the broken one");
        QCOMPARE(::send(good, out.constData(), size_t(out.size()), 0), ssize_t(out.size()));
        QCOMPARE(readDatagram(pair[1]), QByteArray("after the broken one"));
        ::close(good);
        port.close();
        ::close(pair[0]);
        ::close(pair[1]);
    }

    // The whole way a CDJ's frames go: its stream, its member on a link, and
    // a deck on the same link; and back.
    void aCdjAndADeckShareALink() {
        Link cdj("booth", "cdj-a"), deck("booth", "deck-a");
        QString error;
        QVERIFY2(cdj.open(&error), qPrintable(error));
        QVERIFY2(deck.open(&error), qPrintable(error));
        StreamPort port(m_dir.path() + "/c.sock", cdj.qemuFd());
        QVERIFY2(port.open(&error), qPrintable(error));
        const int qemu = connectTo(m_dir.path() + "/c.sock");
        QVERIFY(qemu >= 0);
        QThread::msleep(1200); // the links rescan their members once a second
        const QByteArray hello = QByteArray::fromHex("ffffffffffff0243440a0b0c88b5") + "a CDJ's broadcast";
        const QByteArray out = framed(hello);
        QCOMPARE(::send(qemu, out.constData(), size_t(out.size()), 0), ssize_t(out.size()));
        QCOMPARE(readDatagram(deck.qemuFd()), hello);
        QCOMPARE(::send(deck.qemuFd(), kArp.constData(), size_t(kArp.size()), 0), ssize_t(kArp.size()));
        QCOMPARE(readStream(qemu, 4 + int(kArp.size())), framed(kArp));
        ::close(qemu);
    }
};

QTEST_GUILESS_MAIN(TestCdj)
#include "tst_cdj.moc"
