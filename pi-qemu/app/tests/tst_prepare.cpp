// pipeline/prepare: a unit file given its serial, keys kept in their order;
// config.txt compared the way a release reads it.

#include "pipeline/prepare.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

class TestPrepare : public QObject {
    Q_OBJECT

private slots:
    void newUnitFile() {
        const QString t = prepare::withSerial({}, "trimixxx9", "0xb792f56d");
        const QJsonObject o = QJsonDocument::fromJson(t.toUtf8()).object();
        QCOMPARE(o.value("deck").toString(), QString("trimixxx9"));
        QCOMPARE(o.value("serial").toString(), QString("0xb792f56d"));
        QVERIFY(t.endsWith("}\n"));
    }
    void serialAddedLast() {
        const QString before = "{\n  \"deck\": \"trimixxx2\",\n  \"jogReversed\": true,\n  \"buttons\": {\n    \"0x02\": \"0x03\"\n  }\n}\n";
        const QString after = prepare::withSerial(before, "trimixxx2", "0x1234abcd");
        QCOMPARE(after, QString("{\n  \"deck\": \"trimixxx2\",\n  \"jogReversed\": true,\n  \"buttons\": {\n    \"0x02\": \"0x03\"\n  },\n"
                                "  \"serial\": \"0x1234abcd\"\n}\n"));
        QVERIFY(!QJsonDocument::fromJson(after.toUtf8()).isNull());
    }
    void serialReplacedInPlace() {
        const QString before = "{\n  \"deck\": \"x\",\n  \"serial\": \"0x00000000\",\n  \"hotspotChannel\": 8\n}\n";
        QCOMPARE(prepare::withSerial(before, "x", "0xdeadbeef"),
                 QString("{\n  \"deck\": \"x\",\n  \"serial\": \"0xdeadbeef\",\n  \"hotspotChannel\": 8\n}\n"));
    }
    void configLinesUpToTheRelease() {
        const QString config = "# a comment\n"
                               "dtparam=audio=off   \n"
                               "\n"
                               "[all]\n"
                               "dtoverlay=vc4-kms-dsi-waveshare-panel,10_1_inch # the panel\n"
                               "# Each deck's own lines, under its board's serial\n"
                               "[0xb792f56d]\n";
        QCOMPARE(prepare::configLines(config), (QStringList{"dtparam=audio=off", "dtoverlay=vc4-kms-dsi-waveshare-panel,10_1_inch"}));
    }
};

QTEST_GUILESS_MAIN(TestPrepare)
#include "tst_prepare.moc"
