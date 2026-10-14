// util/: secrets.env's quoting, release versions from tags, shell quoting,
// cards unpacked from .xz.

#include "util/files.h"
#include "util/git.h"
#include "util/process.h"
#include "util/sshtarget.h"
#include "util/envfile.h"
#include "util/fail.h"

#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <fcntl.h>
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
    // A release's card ships as .xz, and an emulated deck gets it unpacked:
    // sparse, its zeros not written (but those it ends with: dd writes them),
    // its size whole; a damaged one fails rather than leave half a card.
    void unxzCards() {
        QTemporaryDir dir;
        const QString card = dir.filePath("card.img"), xz = card + ".xz", out = dir.filePath("deck.img");
        constexpr int M = 1024 * 1024;
        QByteArray content(3 * M, '\x5a');      // 3 MiB of data
        content += QByteArray(60 * M, '\0');    // 60 MiB of zeros
        content += QByteArray(M, '\xa5');       // 1 MiB of data
        content += QByteArray(8 * M + 100, '\0'); // zeros to the end, not a whole MiB
        files::write(card, content);
        QCOMPARE(proc::run("xz", {"-k", "-1", card}), 0);
        files::unxz(xz, out);
        QCOMPARE(files::read(out), content);
        const int fd = ::open(QFile::encodeName(out).constData(), O_RDONLY);
        QCOMPARE(qint64(::lseek(fd, 0, SEEK_HOLE)), qint64(3 * M)); // the 60 MiB of zeros: a hole
        QCOMPARE(qint64(::lseek(fd, 3 * M, SEEK_DATA)), qint64(63 * M));
        ::close(fd);
        QVERIFY(!QFileInfo::exists(out + ".part"));

        files::write(xz, files::read(xz).left(2000)); // cut short
        QVERIFY_THROWS_EXCEPTION(Failure, files::unxz(xz, out));
        QCOMPARE(files::read(out), content); // the deck's card it had stays
        QVERIFY(!QFileInfo::exists(out + ".part"));
    }
};

QTEST_GUILESS_MAIN(TestUtil)
#include "tst_util.moc"
