// s3/protocol and s3/controls: the bytes a deck's S3 sends for each control,
// on its wiring.

#include "s3/controls.h"
#include "s3/protocol.h"
#include "s3/wiring.h"
#include "util/fail.h"

#include <QTest>

namespace {

QByteArray bytes(std::initializer_list<int> b) {
    QByteArray out;
    for (int x : b) out += char(x);
    return out;
}

} // namespace

class TestControls : public QObject {
    Q_OBJECT
    Wiring standard, swapped;

private slots:
    void initTestCase() {
        QString err;
        QVERIFY(standard.load(PI_QEMU_TEST_DATA "/units", "nosuchdeck", &err)); // no file: standard wiring
        QVERIFY2(swapped.load(PI_QEMU_TEST_DATA "/units", "testdeck", &err), qPrintable(err));
    }
    void buttons() {
        QCOMPARE(s3::button(0x3C, true), bytes({0x90, 0x3C, 0x7F}));
        QCOMPARE(s3::button(0x3C, false), bytes({0x80, 0x3C, 0x00}));
    }
    void jogChunksOf63() {
        QCOMPARE(s3::jog(100), bytes({0xB0, 0x11, 63, 0xB0, 0x11, 37}));
        QCOMPARE(s3::jog(-2), bytes({0xB0, 0x11, 0x7E}));
        QVERIFY(s3::jog(0).isEmpty());
    }
    void encoderAndTempo() {
        QCOMPARE(s3::encoder(2), bytes({0xB0, 0x10, 1, 0xB0, 0x10, 1}));
        QCOMPARE(s3::encoder(-1), bytes({0xB0, 0x10, 127}));
        QCOMPARE(s3::tempo(8192), bytes({0xB0, 0x12, 64, 0xB0, 0x32, 0}));
    }
    void parseHex() {
        QByteArray b;
        QVERIFY(s3::parseHex({"90", "7a", "7F"}, &b));
        QCOMPARE(b, bytes({0x90, 0x7A, 0x7F}));
        QVERIFY(!s3::parseHex({"90", "zz"}, &b));
        QVERIFY(!s3::parseHex({"100"}, &b));
    }
    void pressOnTheDecksWiring() {
        auto s = s3::sequence({"press", "play"}, standard);
        QVERIFY(s);
        QCOMPARE(s->steps.size(), 2);
        QCOMPARE(s->steps[0].bytes, bytes({0x90, 0x3C, 0x7F}));
        QCOMPARE(s->steps[0].holdMs, 80);
        QCOMPARE(s->steps[1].bytes, bytes({0x80, 0x3C, 0x00}));
        // On a deck wired the other way round, play is where cue is.
        auto t = s3::sequence({"press", "play", "600ms"}, swapped);
        QCOMPARE(t->steps[0].bytes, bytes({0x90, 0x3D, 0x7F}));
        QCOMPARE(t->steps[0].holdMs, 600);
    }
    void jogFollowsTheEncodersWiring() {
        QCOMPARE(s3::sequence({"jog", "10"}, standard)->steps[0].bytes, s3::jog(10));
        QCOMPARE(s3::sequence({"jog", "10"}, swapped)->steps[0].bytes, s3::jog(-10));
    }
    void tempoCenterAndTouch() {
        QCOMPARE(s3::sequence({"tempo", "center"}, standard)->steps[0].bytes, s3::tempo(8192));
        QCOMPARE(s3::sequence({"touch", "on"}, standard)->steps[0].bytes, bytes({0x90, 0x42, 0x7F}));
    }
    void notAControl() {
        QVERIFY(!s3::sequence({"leds"}, standard));
        QVERIFY(!s3::sequence({}, standard));
    }
    void badWords_data() {
        QTest::addColumn<QStringList>("words");
        QTest::newRow("unknown control") << QStringList{"press", "nosuch"};
        QTest::newRow("no control") << QStringList{"press"};
        QTest::newRow("jog word") << QStringList{"jog", "left"};
        QTest::newRow("tempo range") << QStringList{"tempo", "20000"};
        QTest::newRow("touch word") << QStringList{"touch", "maybe"};
        QTest::newRow("midi word") << QStringList{"midi", "9G"};
    }
    void badWords() {
        QFETCH(QStringList, words);
        try {
            s3::sequence(words, standard);
            QFAIL("no failure");
        } catch (const Failure& f) {
            QCOMPARE(f.status, 2);
        }
    }
    void feedbackLights() {
        s3::Feedback fb;
        int changes = 0;
        fb.onLeds = [&] { changes++; };
        fb.feed(bytes({0x90, 0x3C, 0x7F})); // Mixxx lights PLAY
        QVERIFY(fb.leds().play);
        QVERIFY(changes > 0);
        fb.feed(bytes({0x90, 0x3C, 0x00}));
        QVERIFY(!fb.leds().play);
    }
};

QTEST_GUILESS_MAIN(TestControls)
#include "tst_controls.moc"
