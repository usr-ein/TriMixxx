// cli/args: how a verb's words are parsed.

#include "cli/args.h"
#include "util/fail.h"

#include <QTest>

using cli::Args;
using cli::Option;

class TestArgs : public QObject {
    Q_OBJECT
    const QVector<Option> opts{{"window", {}, ""}, {"from", "CARD", ""}, {"host", "ALIAS", ""}};

private slots:
    void flagsValuesAndPositionals() {
        Args a = Args::parse({"agent1", "--window", "--from", "x.img", "extra"}, opts);
        QVERIFY(a.has("window"));
        QCOMPARE(a.value("from"), QString("x.img"));
        QCOMPARE(a.positional(), (QStringList{"agent1", "extra"}));
        QVERIFY(!a.has("host"));
    }
    void inlineValue() {
        Args a = Args::parse({"--from=a=b.img"}, opts);
        QCOMPARE(a.value("from"), QString("a=b.img"));
    }
    void afterDashes() {
        Args a = Args::parse({"agent1", "--", "--audio", "wav:/x.wav"}, opts);
        QCOMPARE(a.positional(), QStringList{"agent1"});
        QCOMPARE(a.afterDashes(), (QStringList{"--audio", "wav:/x.wav"}));
    }
    void optionsEndAfterTarget() {
        // deck ssh NAME ls -la: the command keeps its own dashes.
        Args a = Args::parse({"agent1", "ls", "-la", "--window"}, opts, 1);
        QCOMPARE(a.positional(), (QStringList{"agent1", "ls", "-la", "--window"}));
        QVERIFY(!a.has("window"));
        // deck ssh --host trimixxx-pi uptime -p
        Args b = Args::parse({"--host", "trimixxx-pi", "uptime", "-p"}, opts, 1);
        QCOMPARE(b.value("host"), QString("trimixxx-pi"));
        QCOMPARE(b.positional(), (QStringList{"uptime", "-p"}));
    }
    void negativeNumbersArePositionals() {
        Args a = Args::parse({"agent1", "-120"}, opts);
        QCOMPARE(a.positional(), (QStringList{"agent1", "-120"}));
        QCOMPARE(a.take("NAME"), QString("agent1"));
        QCOMPARE(a.takeNumber("TICKS"), -120);
    }
    void help() {
        QVERIFY(Args::parse({"--help"}, opts).has("help"));
        QVERIFY(Args::parse({"x", "-h"}, opts).has("help"));
    }
    void usageErrors_data() {
        QTest::addColumn<QStringList>("words");
        QTest::newRow("unknown option") << QStringList{"--nope"};
        QTest::newRow("unknown short") << QStringList{"-x"};
        QTest::newRow("missing value") << QStringList{"--from"};
        QTest::newRow("flag with value") << QStringList{"--window=1"};
    }
    void usageErrors() {
        QFETCH(QStringList, words);
        try {
            Args::parse(words, opts);
            QFAIL("no failure");
        } catch (const Failure& f) {
            QCOMPARE(f.status, 2);
        }
    }
    void takeAndDone() {
        Args a = Args::parse({"one"}, opts);
        QCOMPARE(a.takeOr("default"), QString("one"));
        QCOMPARE(a.takeOr("default"), QString("default"));
        QCOMPARE(a.takeNumber("SECONDS", 120), 120);
        a.done();
        Args b = Args::parse({"one", "two"}, opts);
        b.take("NAME");
        QVERIFY_THROWS_EXCEPTION(Failure, b.done());
        Args c = Args::parse({"ten"}, opts);
        QVERIFY_THROWS_EXCEPTION(Failure, c.takeNumber("SECONDS"));
    }
};

QTEST_GUILESS_MAIN(TestArgs)
#include "tst_args.moc"
