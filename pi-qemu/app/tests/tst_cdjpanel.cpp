// cdj/panel, cdj/panelclient and ui/cdjwindow: an emulated CDJ's keys and
// lamps, the control channel held open, and the window's pads. A fake channel
// stands in for the emulator; the names are checked against the emulator's
// own tables when its submodule is checked out.

#include "cdj/cdj.h"
#include "cdj/panel.h"
#include "cdj/panelclient.h"
#include "ui/cdjwindow.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

namespace {

const QString kEmulator = QStringLiteral(PI_QEMU_SOURCE_DIR "/../cdj2000-emulator");

QString source(const QString& path) {
    QFile f(kEmulator + "/" + path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// The emulator's control channel, as far as a client sees it: a greeting,
// then one answer per line, in order; `state` answered with `stateReply`.
class FakeChannel : public QObject {
public:
    QTcpServer  server;
    QStringList lines; // everything received, in order
    QHash<QString, qint64> when; // the last time each line came, in ms
    QMap<QString, QString> analog; // the analogue fields set, by number
    QString     stateReply = "ok state frames=1 lamps=-";
    QList<QTcpSocket*> clients;
    QElapsedTimer clock;

    FakeChannel() {
        clock.start();
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = server.nextPendingConnection()) {
                clients << s;
                s->write("ok cdj2000-input\n");
                connect(s, &QTcpSocket::readyRead, this, [this, s] {
                    while (s->canReadLine()) {
                        const QString line = QString::fromUtf8(s->readLine()).trimmed();
                        lines << line;
                        when[line] = clock.elapsed();
                        const QStringList w = line.split(' ');
                        if (w.size() == 3 && w[0] == "analog") analog[w[1]] = w[2]; // as the channel keeps them
                        QString reply = "ok";
                        if (line == "state") {
                            reply = stateReply;
                            for (auto it = analog.cbegin(); it != analog.cend(); ++it)
                                reply += QString(" a%1=%2/%2").arg(it.key(), it.value());
                        }
                        s->write(reply.toUtf8() + "\n");
                    }
                });
            }
        });
    }
    quint16 port() const { return server.serverPort(); }
    QStringList contacts() const { // what was sent but `state`
        QStringList out;
        for (const QString& l : lines)
            if (l != "state") out << l;
        return out;
    }
};

const CdjPlace* placeOf(const QString& key) {
    for (const CdjPlace& p : cdjPlaces())
        if (p.key == key) return &p;
    return nullptr;
}

} // namespace

class TestCdjPanel : public QObject {
    Q_OBJECT

private slots:
    void everyKeyPressKnows() {
        QCOMPARE(cdj::keys().size(), 44);
        QSet<QString> names;
        QSet<QPair<int, int>> contacts;
        for (const cdj::Key& k : cdj::keys()) {
            names << k.name;
            contacts << qMakePair(k.byte, int(k.mask));
            QVERIFY(k.byte >= 15 && k.byte <= 21);
            QCOMPARE(k.mask & (k.mask - 1), 0); // one bit
        }
        QCOMPARE(names.size(), 44);
        QCOMPARE(contacts.size(), 44);
    }
    void keysAreTheEmulators() {
        // nxs_panel.KEY_NAMES: panel_control's names for bytes 15, 16, 17 and
        // 20, with the NXS's own over them.
        const QString legacy = source("tools/cdj_main/panel_control.py"), nxs = source("tools/cdj_main/nxs_panel.py");
        if (legacy.isEmpty() || nxs.isEmpty()) QSKIP("cdj2000-emulator is not checked out here");
        QHash<QPair<int, int>, QString> want;
        const QRegularExpression entry("\\((\\d+),\\s*(\\d+)\\):\\s*\"([^\"]+)\"");
        const QString table = legacy.section("FIRMWARE_KEY_NAMES", 1, 1).section("}", 0, 0);
        for (auto m = entry.globalMatch(table); m.hasNext();) {
            const auto e = m.next();
            if (QList<int>{15, 16, 17, 20}.contains(e.captured(1).toInt()))
                want.insert({e.captured(1).toInt(), e.captured(2).toInt()}, e.captured(3));
        }
        for (auto m = entry.globalMatch(nxs.section("KEY_NAMES.update", 1, 1).section("})", 0, 0)); m.hasNext();) {
            const auto e = m.next();
            want.insert({e.captured(1).toInt(), e.captured(2).toInt()}, e.captured(3));
        }
        QCOMPARE(want.size(), cdj::keys().size());
        for (const cdj::Key& k : cdj::keys()) {
            const int bit = qCountTrailingZeroBits(uint(k.mask));
            QCOMPARE(want.value({k.byte, bit}), k.name);
        }
    }
    void keysByTheNamesPressTakes() {
        QCOMPARE(cdj::key("play")->name, QString("PLAY"));
        QCOMPARE(cdj::key("hot-cue-a")->name, QString("HOT CUE A"));
        QCOMPARE(cdj::key("Tag_List")->name, QString("TAG LIST"));
        QCOMPARE(cdj::key("enter")->name, QString("ENCODER PUSH"));
        QCOMPARE(cdj::key("back")->name, QString("RETURN"));
        QCOMPARE(cdj::key("info")->name, QString("INFORMATION"));
        QVERIFY(!cdj::key("scratch"));
        QCOMPARE(cdj::downLine(*cdj::key("play")), QString("down 16 01"));
        QCOMPARE(cdj::upLine(*cdj::key("master")), QString("up 21 04"));
        QCOMPARE(cdj::rotaryLine(-3), QString("rotary 7 -3"));
    }
    void lampsFromState() {
        const auto lamps = cdj::lampsOf("ok state frames=9 held=00 lamps=PLAY_PAUSE:blink,SOURCE_USB:3,MASTER:1,CUE:on "
                                        "last_press=1:2-3");
        QCOMPARE(lamps.size(), 4);
        QCOMPARE(cdj::lit(lamps, "PLAY_PAUSE"), cdj::Lit::Blink);
        QCOMPARE(cdj::lit(lamps, "SOURCE_USB"), cdj::Lit::On);
        QCOMPARE(cdj::lit(lamps, "MASTER"), cdj::Lit::Dim);
        QCOMPARE(cdj::lit(lamps, "CUE"), cdj::Lit::On);
        QCOMPARE(cdj::lit(lamps, "SYNC"), cdj::Lit::Off);
        QVERIFY(cdj::lampsOf("ok state frames=9 lamps=-").isEmpty());
        QVERIFY(cdj::lampsOf("err unknown command").isEmpty());
    }
    void lampsAreTheEmulators() {
        // Every lamp a pad shows is one the emulator names for the NXS, and
        // the window's two-bit lamps are its two-bit fields.
        const QString lamps = source("emulator/qemu/cdj2000_lamps.c");
        if (lamps.isEmpty()) QSKIP("cdj2000-emulator is not checked out here");
        QHash<QString, int> named; // name -> bits in its mask
        const QRegularExpression entry("\\{\\s*(\\d+),\\s*0x([0-9a-f]+),\\s*\"([A-Z_]+)\"\\s*\\}");
        for (auto m = entry.globalMatch(lamps.section("nxs_lamp_names[]", 1, 1).section("};", 0, 0)); m.hasNext();) {
            const auto e = m.next();
            named.insert(e.captured(3), qPopulationCount(e.captured(2).toUInt(nullptr, 16)));
        }
        QVERIFY(named.contains("PLAY_PAUSE") && named.contains("MASTER"));
        for (const CdjPlace& place : cdjPlaces())
            for (const CdjPlace::Lamp& lamp : place.lamps) {
                QVERIFY2(named.contains(lamp.name), qPrintable(lamp.name));
                QCOMPARE(cdj::twoBit(lamp.name), named.value(lamp.name) == 2);
            }
    }
    void everyKeyHasItsPlace() {
        QSet<QString> placed;
        for (const CdjPlace& place : cdjPlaces()) {
            QVERIFY2(cdj::key(place.key) || place.shape == CdjPlace::Shape::Fader, qPrintable(place.key));
            QVERIFY2(!placed.contains(place.key), qPrintable(place.key));
            placed << place.key;
            QVERIFY2(kCdjPlate.contains(place.rect), qPrintable(place.key));
            QVERIFY2(!place.rect.intersects(kCdjScreen.adjusted(-6, -6, 6, 6)), qPrintable(place.key));
            for (const CdjPlace& other : cdjPlaces())
                if (&other != &place) QVERIFY2(!place.rect.intersects(other.rect), qPrintable(place.key + " " + other.key));
        }
        for (const cdj::Key& k : cdj::keys()) QVERIFY2(placed.contains(k.name), qPrintable(k.name));
        QVERIFY(placed.contains("TEMPO")); // and the fader
    }
    void stateFields() {
        const QString reply = "ok state frames=7 commands=2 queue=0 phase=idle held=00 "
                              "level_mask=00000000000000000000000000000002000000000000 "
                              "level_value=00000000000000000000000000000000000000000000 "
                              "a0=-0/0 a1=-0/0 a2=16384/16384 a3=32768/32768 a7=8/8 sd_lid=open lamps=CUE:on";
        const auto state = cdj::stateOf(reply);
        QCOMPARE(state.value("sd_lid"), QString("open"));
        QCOMPARE(cdj::analogTarget(state, 2), 16384);
        QCOMPARE(cdj::analogTarget(state, 3), 32768);
        QCOMPARE(cdj::analogTarget(state, 0), -1); // nobody drives it
        QVERIFY(cdj::leverAtRev(state));        // 15.1 forced low: REV
        auto fwd = state;
        fwd["level_value"] = "00000000000000000000000000000002000000000000";
        QVERIFY(!cdj::leverAtRev(fwd));
        fwd["level_mask"] = QString(44, '0');
        QVERIFY(!cdj::leverAtRev(fwd));         // no override: the base frame's FWD
        QVERIFY(cdj::stateOf("ok pong").isEmpty());
        QCOMPARE(cdj::tempoCentreLine(), QString("analog 3 32768"));
        QCOMPARE(cdj::tempoLine(70000), QString("analog 2 65535"));
    }
    void clientPollsAndSends() {
        FakeChannel channel;
        channel.stateReply = "ok state frames=3 lamps=CUE:on,SOURCE_SD:3";
        cdj::PanelClient client(channel.port(), 20);
        QSignalSpy lamps(&client, &cdj::PanelClient::lampsChanged);
        QTRY_VERIFY(client.connected());
        QTRY_COMPARE(lamps.size(), 1);
        using Lamps = QHash<QString, QString>;
        QCOMPARE(lamps.first().first().value<Lamps>().value("SOURCE_SD"), QString("3"));
        // The same lamps again are no news.
        QTest::qWait(100);
        QCOMPARE(lamps.size(), 1);
        client.send("down 16 01");
        QTRY_COMPARE(channel.contacts(), QStringList{"down 16 01"});
        // Polled, but never a second `state` before the first is answered.
        QVERIFY(channel.lines.count("state") >= 2);
    }
    void clientComesBack() {
        FakeChannel channel;
        cdj::PanelClient client(channel.port(), 20);
        QSignalSpy connected(&client, &cdj::PanelClient::connectedChanged);
        QTRY_VERIFY(client.connected());
        channel.clients.first()->disconnectFromHost();
        QTRY_VERIFY(!client.connected());
        QTRY_VERIFY_WITH_TIMEOUT(client.connected(), 3000);
        QCOMPARE(connected.size(), 3); // up, down, up
    }
    void aKeyLetGoOfWhileTheChannelIsAwayIsLetGoOfOnceBack() {
        // A key let go of in a window while the channel reconnects would stay
        // down on the CDJ, whose held contacts outlive a connection.
        FakeChannel channel;
        cdj::PanelClient client(channel.port(), 1000);
        QTRY_VERIFY(client.connected());
        channel.clients.first()->disconnectFromHost();
        QTRY_VERIFY(!client.connected());
        client.send("up 16 01");
        client.send("down 16 02"); // a click on a dead panel: dropped
        client.send("up 16 01");
        QTRY_VERIFY_WITH_TIMEOUT(client.connected(), 3000);
        QTRY_COMPARE(channel.contacts(), QStringList{"up 16 01"});
    }
    void aClickHoldsTheKeyAtLeastAsLongAsAPress() {
        FakeChannel channel;
        QTemporaryDir run;
        CdjWindow w("test", run.path(), channel.port());
        w.resize(800, 600); // one plate unit, one pixel
        w.show();
        QTRY_COMPARE(channel.lines.count("state") > 0, true);
        const QPoint play = placeOf("PLAY")->rect.center().toPoint();
        QTest::mouseClick(&w, Qt::LeftButton, {}, play);
        QTRY_COMPARE(channel.contacts(), QStringList{"down 16 01"});
        QTRY_COMPARE(channel.contacts(), (QStringList{"down 16 01", "up 16 01"}));
        // Held about 100 ms though the click was instant: as received here,
        // where a line can wait a few ms for this test's own event loop.
        QVERIFY(channel.when["up 16 01"] - channel.when["down 16 01"] >= 50);
        // Shift-click latches it down until the next one.
        const QPoint cue = placeOf("CUE")->rect.center().toPoint();
        QTest::mouseClick(&w, Qt::LeftButton, Qt::ShiftModifier, cue);
        QTest::qWait(250);
        QCOMPARE(channel.contacts().last(), QString("down 16 02"));
        QTest::mouseClick(&w, Qt::LeftButton, Qt::ShiftModifier, cue);
        QTRY_COMPARE(channel.contacts().last(), QString("up 16 02"));
    }
    void aWindowGoneLetsGoOfItsKeys() {
        // The channel's held contacts are the panel's: a key the window held
        // when it went would stay down on the CDJ, and the next press of it
        // would be no press at all.
        FakeChannel channel;
        QTemporaryDir run;
        {
            CdjWindow w("test", run.path(), channel.port());
            w.resize(800, 600);
            w.show();
            QTRY_COMPARE(channel.lines.count("state") > 0, true);
            QTest::mouseClick(&w, Qt::LeftButton, Qt::ShiftModifier, placeOf("CUE")->rect.center().toPoint());
            QTest::mousePress(&w, Qt::LeftButton, {}, placeOf("PLAY")->rect.center().toPoint());
            QTRY_COMPARE(channel.contacts(), (QStringList{"down 16 02", "down 16 01"}));
        }
        auto sent = [&channel] {
            const QStringList lines = channel.contacts();
            return QSet<QString>(lines.begin(), lines.end());
        };
        QTRY_COMPARE(sent(), (QSet<QString>{"down 16 02", "down 16 01", "up 16 02", "up 16 01"}));
    }
    void theFaderSetsTheCentreThenThePosition() {
        // As the NXS takes its slider: the centre (field 3) first, then the
        // position (field 2), which the tempo follows when it changes.
        FakeChannel channel;
        QTemporaryDir run;
        CdjWindow w("test", run.path(), channel.port());
        w.resize(800, 600);
        w.show();
        QTRY_COMPARE(channel.lines.count("state") > 0, true);
        const QRectF fader = placeOf("TEMPO")->rect;
        const QPoint top(int(fader.center().x()), int(fader.top() + 22)), bottom(top.x(), int(fader.bottom() - 16));
        // Its top is the minus end, 0, where the field already is: a nudge
        // first, so the CDJ sees it change.
        QTest::mousePress(&w, Qt::LeftButton, {}, top);
        QTest::mouseMove(&w, bottom);
        QTest::mouseRelease(&w, Qt::LeftButton, {}, bottom);
        QTRY_COMPARE(channel.contacts(),
                     (QStringList{"analog 3 32768", "analog 2 1", "analog 2 0", "analog 2 65535"}));
        QTest::mouseDClick(&w, Qt::LeftButton, {}, bottom); // the centre detent
        QTRY_COMPARE(channel.contacts().last(), QString("analog 2 32768"));
        QCOMPARE(channel.contacts().count("analog 3 32768"), 1);
    }
    void theSwitchesAndTheFaderStartAsTheCdjHasThem() {
        FakeChannel channel;
        channel.stateReply = "ok state frames=7 "
                             "level_mask=00000000000000000000000000000002000000000000 "
                             "level_value=00000000000000000000000000000000000000000000 "
                             "a2=65535/65535 a3=32768/32768 sd_lid=open lamps=-";
        QTemporaryDir run;
        CdjWindow w("test", run.path(), channel.port());
        w.resize(800, 600);
        w.show();
        QTRY_COMPARE(channel.lines.count("state") > 1, true);
        // The fader's knob at its plus end, as another client left it.
        const QRectF fader = placeOf("TEMPO")->rect;
        QTRY_COMPARE(w.grab().toImage().pixelColor(int(fader.center().x()), int(fader.bottom() - 16)),
                     QColor(200, 200, 205));
        // The lever at REV and the lid open: a click puts them back.
        QTest::mouseClick(&w, Qt::LeftButton, {}, placeOf("REV")->rect.center().toPoint());
        QTest::mouseClick(&w, Qt::LeftButton, {}, placeOf("SD OPEN")->rect.center().toPoint());
        QTRY_COMPARE(channel.contacts(), (QStringList{"level 15 02 1", "sd-lid closed"}));
    }
    void theSelectorTurnsAndPushes() {
        FakeChannel channel;
        QTemporaryDir run;
        CdjWindow w("test", run.path(), channel.port());
        w.resize(800, 600);
        w.show();
        QTRY_COMPARE(channel.lines.count("state") > 0, true);
        const QPoint knob = placeOf("ENCODER PUSH")->rect.center().toPoint();
        QWheelEvent down(knob, w.mapToGlobal(knob), {}, {0, -240}, Qt::NoButton, {}, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&w, &down);
        QTRY_COMPARE(channel.contacts(), QStringList{"rotary 7 2"});
        QTest::mouseClick(&w, Qt::LeftButton, {}, knob);
        QTRY_COMPARE(channel.contacts(), (QStringList{"rotary 7 2", "down 17 01", "up 17 01"}));
        // A trackpad's small steps add up to whole detents, back the other way.
        for (int i = 0; i < 10; i++) {
            QWheelEvent step(knob, w.mapToGlobal(knob), {}, {0, 24}, Qt::NoButton, {}, Qt::NoScrollPhase, false);
            QApplication::sendEvent(&w, &step);
        }
        QTRY_COMPARE(channel.contacts().mid(3), (QStringList{"rotary 7 -1", "rotary 7 -1"}));
    }
    void thePadsLightAsTheLampsSay() {
        FakeChannel channel;
        channel.stateReply = "ok state frames=3 lamps=PLAY_PAUSE:on,SOURCE_USB:1";
        QTemporaryDir run;
        CdjWindow w("test", run.path(), channel.port());
        w.resize(800, 600);
        w.show();
        auto at = [&w](const QString& key) {
            const QRectF r = placeOf(key)->rect;
            return w.grab().toImage().pixelColor((r.topLeft() + QPointF(6, r.height() / 2)).toPoint());
        };
        QTRY_COMPARE(at("PLAY"), QColor(60, 220, 60));   // lit green
        const QColor dim = at("USB"), dark = at("LINK"); // available, and dark
        QVERIFY(dim.lightness() > dark.lightness());
        channel.stateReply = "ok state frames=4 lamps=-";
        QTRY_VERIFY(at("PLAY") != QColor(60, 220, 60));
    }
    void theScreenIsTheCdjs() {
        FakeChannel channel;
        QTemporaryDir run;
        QImage frame(480, 255, QImage::Format_RGB888);
        frame.fill(QColor(255, 0, 0));
        QVERIFY(frame.save(run.filePath("screen.ppm"), "PPM"));
        CdjWindow w("test", run.path(), channel.port());
        w.resize(800, 600);
        w.show();
        QTRY_COMPARE(w.grab().toImage().pixelColor(kCdjScreen.center().toPoint()), QColor(255, 0, 0));
        // A new frame, published as the GUI board does: written aside, renamed.
        frame.fill(QColor(0, 0, 255));
        QVERIFY(frame.save(run.filePath("screen.tmp"), "PPM"));
        QFile::remove(run.filePath("screen.ppm"));
        QVERIFY(QFile::rename(run.filePath("screen.tmp"), run.filePath("screen.ppm")));
        QTRY_COMPARE(w.grab().toImage().pixelColor(kCdjScreen.center().toPoint()), QColor(0, 0, 255));
    }
    void aLiveCdj() {
        // Opt in with a CDJ of your own, up with its test card:
        //   pi-qemu cdj up NAME --test-track
        //   PI_QEMU_CDJ_LIVE=NAME [PI_QEMU_CDJ_SHOTS=DIR] tst_cdjpanel aLiveCdj
        // or with a track of your own loaded and playing (cdj up --dsp-model),
        // and PI_QEMU_CDJ_LIVE_LOADED=1; or for one click, and the lamp it
        // should light, PI_QEMU_CDJ_LIVE_CLICK=KEY:LAMP:VALUE; or the fader,
        // PI_QEMU_CDJ_LIVE_FADER=POSITION.
        // Clicks in the window drive the CDJ, and what its firmware lights
        // comes back: watched on a second connection, as `cdj press` would be.
        const QString name = qEnvironmentVariable("PI_QEMU_CDJ_LIVE");
        if (name.isEmpty()) QSKIP("PI_QEMU_CDJ_LIVE names no running CDJ");
        const cdj::Cdj c(name);
        QVERIFY2(c.running() && c.panelPort(), "not running");
        CdjWindow w(name, c.runDir(), c.panelPort());
        w.resize(800, 600);
        w.show();
        cdj::PanelClient watch(c.panelPort(), 50);
        QHash<QString, QString> lamps;
        connect(&watch, &cdj::PanelClient::lampsChanged, this, [&lamps](const auto& l) { lamps = l; });
        QTRY_VERIFY(watch.connected());
        const QString shots = qEnvironmentVariable("PI_QEMU_CDJ_SHOTS");
        int shot = 0;
        // Click a key; true once `lamp` shows one of `want` (within `ms`).
        auto click = [&](const QString& key, const QString& lamp, const QStringList& want, int ms = 15000) {
            QTest::mouseClick(&w, Qt::LeftButton, {}, placeOf(key)->rect.center().toPoint());
            QElapsedTimer t;
            t.start();
            while (!want.contains(lamps.value(lamp)) && t.elapsed() < ms) QTest::qWait(50);
            QTest::qWait(1500); // and the screen after it
            if (!shots.isEmpty()) w.grab().save(QString("%1/%2-%3.png").arg(shots).arg(++shot).arg(key));
            qInfo("%s: %s is %s", qPrintable(key), qPrintable(lamp), qPrintable(lamps.value(lamp)));
            return want.contains(lamps.value(lamp));
        };
        if (const QString fader = qEnvironmentVariable("PI_QEMU_CDJ_LIVE_FADER"); !fader.isEmpty()) {
            // The fader clicked where it is POSITION (0..65535): the CDJ's
            // fields as the second connection reads them, and its screen.
            QHash<QString, QString> state;
            connect(&watch, &cdj::PanelClient::stateRead, this, [&state](const auto& s) { state = s; });
            const QRectF r = placeOf("TEMPO")->rect;
            const double top = r.top() + 22, bottom = r.bottom() - 16;
            const QPoint at(int(r.center().x()), int(top + (bottom - top) * fader.toInt() / 65535.0 + 0.5));
            QTest::mouseClick(&w, Qt::LeftButton, {}, at);
            QTRY_VERIFY_WITH_TIMEOUT(cdj::analogTarget(state, 3) == cdj::kTempoCentre &&
                                         qAbs(cdj::analogTarget(state, 2) - fader.toInt()) < 400, 5000);
            QTest::qWait(2500);
            qInfo("fader at %s: a2=%s a3=%s", qPrintable(fader), qPrintable(state.value("a2")), qPrintable(state.value("a3")));
            if (!shots.isEmpty()) w.grab().save(QString("%1/fader-%2.png").arg(shots, fader));
            return;
        }
        if (const QStringList one = qEnvironmentVariable("PI_QEMU_CDJ_LIVE_CLICK").split(':'); one.size() == 3) {
            // One click, and the lamp it should light: "MASTER:MASTER:3".
            QVERIFY(click(one[0], one[1], {one[2]}));
            return;
        }
        if (qEnvironmentVariable("PI_QEMU_CDJ_LIVE_LOADED") == "1") {
            // A track of your own loaded and playing, on `cdj up --dsp-model`:
            // the transport the emulator's own DSP cannot take, as well.
            QVERIFY(click("PLAY", "PLAY_PAUSE", {"blink"})); // paused
            QVERIFY(click("CUE", "CUE", {"on"}));            // a cue point, there
            QVERIFY(click("PLAY", "PLAY_PAUSE", {"on"}));    // played from it
            QVERIFY(click("CUE", "PLAY_PAUSE", {"blink"}));  // CUE while playing: back to it,
            QCOMPARE(lamps.value("CUE"), QString("on"));     // paused there
            QVERIFY(click("PLAY", "PLAY_PAUSE", {"on"}));
            return;
        }
        QVERIFY(click("LINK", "SOURCE_LINK", {"3"}));
        // The card may still be mounting ("NO CARD"): SD again until the
        // second push, into its folder and onto the test track, loads it.
        bool loaded = false;
        for (int attempt = 0; attempt < 6 && !loaded; attempt++) {
            QVERIFY(click("SD", "SOURCE_SD", {"3"}));
            click("ENCODER PUSH", "BROWSE", {"3"}, 2000);
            loaded = click("ENCODER PUSH", "PLAY_PAUSE", {"on"}, 8000); // it plays once loaded
        }
        QVERIFY(loaded);
        QTest::qWait(3000); // a track just loaded takes no PLAY for a moment
        QVERIFY(click("PLAY", "PLAY_PAUSE", {"blink"})); // paused
        QVERIFY(click("PLAY", "PLAY_PAUSE", {"on"}));    // playing again
        QVERIFY(click("PLAY", "PLAY_PAUSE", {"blink"})); // and paused
        // Back to the cue point from the pause: CUE lit. (The emulator's own
        // DSP, without --dsp-model, seeks no further: a CUE while playing, or
        // a PLAY after this one, is a limit of its transport, not the panel's.)
        QVERIFY(click("CUE", "CUE", {"on"}));
    }
};

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TestCdjPanel t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_cdjpanel.moc"
