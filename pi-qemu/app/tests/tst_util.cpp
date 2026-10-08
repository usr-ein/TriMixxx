// util/: secrets.env's quoting, release versions from tags, shell quoting.

#include "util/git.h"
#include "util/process.h"
#include "util/secrets.h"

#include <QTest>

class TestUtil : public QObject {
    Q_OBJECT

private slots:
    void secretsQuoting() {
        const auto s = secrets::parse("# the console\n"
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
