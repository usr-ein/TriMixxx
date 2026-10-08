// util/: secrets.env's quoting, release versions from tags, shell quoting.

#include "util/files.h"
#include "util/git.h"
#include "util/process.h"
#include "util/sshtarget.h"
#include "util/envfile.h"

#include <QTemporaryDir>
#include <QTest>

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
};

QTEST_GUILESS_MAIN(TestUtil)
#include "tst_util.moc"
