// pipeline/identity: a deck's /data, as `release card` makes it.

#include "pipeline/identity.h"
#include "util/files.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

class TestIdentity : public QObject {
    Q_OBJECT
    QTemporaryDir cache;

private slots:
    void initTestCase() {
        QVERIFY(cache.isValid());
        qputenv("PI_QEMU_CACHE", cache.path().toUtf8());
        qputenv("HOME_WIFI_SSID", "Example Net");
        qputenv("HOME_WIFI_PSK", "not-a-real-one");
    }
    void uuidAsPythonMakesIt() {
        // python3 -c 'import uuid; print(uuid.uuid5(uuid.NAMESPACE_DNS, "home-wifi.Example Net"))'
        QCOMPARE(identity::homeWifiUuid("Example Net"), QString("0f385b9b-d9ec-5382-a69d-98bfa8a68801"));
    }
    void aDecksData() {
        const QString d = identity::prepare("trimixxx3"); // its unit file sets hotspotChannel 8
        QCOMPARE(QString::fromUtf8(files::read(d + "/trimixxx.conf")), QString("trimixxx3\n"));
        QVERIFY(!QDir(d + "/ssh").entryList({"ssh_host_ed25519_key"}).isEmpty());
        const QString hotspot = QString::fromUtf8(files::read(d + "/NetworkManager/trimixxx-hotspot.nmconnection"));
        QVERIFY(hotspot.contains("\nssid=trimixxx3\n"));
        QVERIFY(hotspot.contains("\nchannel=8\n"));
        QVERIFY(!hotspot.contains('@'));
        const QFile::Permissions p = QFile::permissions(d + "/NetworkManager/home-wifi.nmconnection");
        QVERIFY(!(p & QFile::ReadOther) && !(p & QFile::ReadGroup));
        const QString home = QString::fromUtf8(files::read(d + "/NetworkManager/home-wifi.nmconnection"));
        QVERIFY(home.contains("\nssid=Example Net\n"));
        QVERIFY(home.contains("\npsk=not-a-real-one\n"));
        QVERIFY(home.contains("\nuuid=0f385b9b-d9ec-5382-a69d-98bfa8a68801\n"));
        QVERIFY(!QFile::exists(d + "/NetworkManager/pi-qemu-home.nmconnection"));
    }
    void keysAreKeptAcrossRuns() {
        const QString d = identity::prepare("trimixxx3");
        const QByteArray key = files::read(d + "/ssh/ssh_host_ed25519_key.pub");
        identity::prepare("trimixxx3");
        QCOMPARE(files::read(d + "/ssh/ssh_host_ed25519_key.pub"), key);
    }
    void theEmulatedDeckGetsItsNic() {
        const QString d = identity::prepare("trimixxx0");
        QVERIFY(QFile::exists(d + "/NetworkManager/pi-qemu-home.nmconnection"));
    }
};

QTEST_GUILESS_MAIN(TestIdentity)
#include "tst_identity.moc"
