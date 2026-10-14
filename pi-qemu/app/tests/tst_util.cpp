// util/: secrets.env's quoting, release versions from tags, shell quoting,
// sparse cards.

#include "util/files.h"
#include "util/git.h"
#include "util/process.h"
#include "util/sshtarget.h"
#include "util/envfile.h"

#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

class TestUtil : public QObject {
    Q_OBJECT

private slots:
    void secretsQuoting() {
        const auto s = envfile::parse("# the console\n"
                                      "SAM1902_PASSWORD='p a$s'\n"
                                      "export HOME_WIFI_SSID=\"Odd \\\"name\\\"\"\n"
                                      "HOME_WIFI_PSK=bare # comment\n"
                                      "  NOT AN ASSIGNMENT\n");
        QCOMPARE(s.value("SAM1902_PASSWORD"), QString("p a$s"));
        QCOMPARE(s.value("HOME_WIFI_SSID"), QString("Odd \"name\""));
        QCOMPARE(s.value("HOME_WIFI_PSK"), QString("bare"));
        QCOMPARE(s.size(), 3);
    }
    void versionsFromTags() {
        QCOMPARE(git::newestVersion({"pi/v1.9.2", "pi/v1.10.0", "midi-s3/v9.0.0", "pi/vbeta"}, "pi/v"), QString("1.10.0"));
        QCOMPARE(git::newestVersion({"midi-s3/v1.0.0"}, "pi/v"), QString());
        QCOMPARE(git::newestVersion({"pi/v1.0"}, "pi/v"), QString());
    }
    // A real deck's wrappers: `deck` and `deck:PATH` become its alias, every
    // other word stays as it is (checked with exec swapped for printf).
    void realDeckWrappers() {
        QTemporaryDir dir;
        SshTarget{"trimixxx-pi", {}}.writeWrappers(dir.path());
        for (const QString tool : {"ssh", "scp"}) {
            QString body = QString::fromUtf8(files::read(dir.filePath(tool)));
            QVERIFY(body.contains("exec /usr/bin/" + tool));
            body.replace("exec /usr/bin/" + tool + " \"$@\"", "printf '%s\\n' \"$@\"");
            files::write(dir.filePath(tool), body.toUtf8());
        }
        proc::Options o;
        o.quiet = true;
        QCOMPARE(proc::capture("sh", {dir.filePath("ssh"), "-o", "ConnectTimeout=2", "deck", "echo deck: hi"}, o).text(),
                 QString("-o\nConnectTimeout=2\ntrimixxx-pi\necho deck: hi"));
        QCOMPARE(proc::capture("sh", {dir.filePath("scp"), "a b", "deck:/tmp/x y", "deckhand:/z"}, o).text(),
                 QString("a b\ntrimixxx-pi:/tmp/x y\ndeckhand:/z"));
    }
    void emulatedDeckWrappers() {
        QTemporaryDir dir;
        SshTarget{"agent1", "/tmp/agent1 cfg/ssh_config"}.writeWrappers(dir.path());
        QCOMPARE(QString::fromUtf8(files::read(dir.filePath("ssh"))),
                 QString("#!/bin/sh\nexec /usr/bin/ssh -F '/tmp/agent1 cfg/ssh_config' \"$@\"\n"));
    }
    void shellQuoting() {
        QCOMPARE(proc::quote("plain-word_1.2:/x"), QString("plain-word_1.2:/x"));
        QCOMPARE(proc::quote("two words"), QString("'two words'"));
        QCOMPARE(proc::quote("it's"), QString("'it'\\''s'"));
        QCOMPARE(proc::quote(""), QString("''"));
        QCOMPARE(proc::join({"ssh", "-p", "22 33"}), QString("ssh -p '22 33'"));
    }
    // A card's zeros given back, as QEMU leaves them: written out in full.
    // Every whole 64 KiB block of zeros becomes a hole and nothing else does;
    // the card reads the same, keeps its times, and a clone made before keeps
    // its blocks.
    void sparseCards() {
        QTemporaryDir dir;
        const QString card = dir.filePath("card.img"), twin = dir.filePath("twin.img");
        constexpr int K = 1024;
        QByteArray content;
        auto data = [&](int n) {
            for (int i = 0; i < n; ++i) content += char(1 + (content.size() * 7) % 251);
        };
        auto zero = [&](int n) { content += QByteArray(n, '\0'); };
        data(64 * K);       // [0, 64K)
        zero(1024 * K);     // [64K, 1088K): a hole
        data(4 * K);        // [1088K, 1092K)
        zero(60 * K);       // [1092K, 1152K): in a block with data, kept
        data(68 * K);       // [1152K, 1220K)
        zero(128 * K);      // [1220K, 1348K): only [1280K, 1344K) is a whole block
        data(60 * K);       // [1348K, 1408K)
        zero(8192 * K - 1408 * K); // [1408K, 8M): a hole, up to where the next read starts
        zero(64 * K);       // [8M, 8M+64K): a hole, in the next read
        data(100);          // a tail shorter than a block
        files::write(card, content);
        auto stat = [](const QString& f) {
            struct stat st {};
            ::stat(QFile::encodeName(f).constData(), &st);
            return st;
        };
        const struct timeval then[2] = {{1600000000, 0}, {1600000000, 0}};
        ::utimes(QFile::encodeName(card).constData(), then);
        files::clone(card, twin);
        const qint64 full = stat(card).st_blocks * 512;
        QVERIFY(full >= content.size()); // written out in full

        const files::Sparse s = files::sparsify(card);
        QVERIFY2(s.ok, qPrintable(s.error));
        QCOMPARE(s.before, full);
        QCOMPARE(s.after, qint64(stat(card).st_blocks) * 512);
        QCOMPARE(s.before - s.after, qint64(1024 + 64 + 6784 + 64) * K);
        QCOMPARE(files::read(card), content);
        QCOMPARE(qint64(stat(card).st_mtimespec.tv_sec), qint64(1600000000));
        QVERIFY(!QFileInfo::exists(card + ".sparse"));
        const int fd = ::open(QFile::encodeName(card).constData(), O_RDONLY);
        QCOMPARE(qint64(::lseek(fd, 0, SEEK_HOLE)), qint64(64 * K));
        QCOMPARE(qint64(::lseek(fd, 64 * K, SEEK_DATA)), qint64(1088 * K));
        QCOMPARE(qint64(::lseek(fd, 1088 * K, SEEK_HOLE)), qint64(1280 * K));
        QCOMPARE(qint64(::lseek(fd, 1280 * K, SEEK_DATA)), qint64(1344 * K));
        QCOMPARE(qint64(::lseek(fd, 1344 * K, SEEK_HOLE)), qint64(1408 * K));
        QCOMPARE(qint64(::lseek(fd, 1408 * K, SEEK_DATA)), qint64(8256 * K));
        ::close(fd);
        QCOMPARE(files::read(twin), content); // the clone, untouched
        QCOMPARE(qint64(stat(twin).st_blocks) * 512, full);

        const files::Sparse again = files::sparsify(card); // nothing left to give
        QVERIFY2(again.ok, qPrintable(again.error));
        QCOMPARE(again.after, s.after);
        QCOMPARE(files::read(card), content);

        const files::Sparse none = files::sparsify(dir.filePath("none.img"));
        QVERIFY(!none.ok);
        QVERIFY(none.error.contains("none.img"));
    }
};

QTEST_GUILESS_MAIN(TestUtil)
#include "tst_util.moc"
