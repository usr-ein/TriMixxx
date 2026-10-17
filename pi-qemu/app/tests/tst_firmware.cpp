// board/firmware: which UART the S3 is on, from a board's device tree.

#include "board/firmware.h"

#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

namespace {

// A device tree with just what s3UartOf reads: serial0's alias, and UART3's
// status (what dtoverlay=uart3 sets to okay).
QString tree(const QTemporaryDir& dir, const QString& serial0, const QString& uart3) {
    const QString dts = QString("/dts-v1/;\n"
                                "/ {\n"
                                "  aliases { serial0 = \"%1\"; serial3 = \"/soc/serial@7e201600\"; };\n"
                                "  soc {\n"
                                "    serial@7e201000 { status = \"okay\"; };\n"
                                "    serial@7e215040 { status = \"disabled\"; };\n"
                                "    serial@7e201600 { status = \"%2\"; };\n"
                                "  };\n"
                                "};\n")
                            .arg(serial0, uart3);
    const QString dtb = dir.filePath(QString("%1-%2.dtb").arg(serial0.section('@', 1), uart3));
    QProcess dtc;
    dtc.start("dtc", {"-I", "dts", "-O", "dtb", "-o", dtb});
    dtc.write(dts.toUtf8());
    dtc.closeWriteChannel();
    if (!dtc.waitForFinished() || dtc.exitCode() != 0) return {};
    return dtb;
}

} // namespace

class TestFirmware : public QObject {
    Q_OBJECT

private slots:
    void s3OnWhicheverUartTheCardGivesItsPins() {
        QTemporaryDir dir;
        const QString pl011 = tree(dir, "/soc/serial@7e201000", "disabled");
        if (pl011.isEmpty()) QSKIP("no dtc (brew install dtc)");
        // dtoverlay=disable-bt, the decks' boot flags before the S3 moved:
        // serial0 is the PL011, on GPIO 14/15.
        QCOMPARE(s3UartOf(pl011), BootPlan::S3Uart::Pl011);
        // Bluetooth on the PL011 and enable_uart=1: the mini UART is serial0.
        QCOMPARE(s3UartOf(tree(dir, "/soc/serial@7e215040", "disabled")), BootPlan::S3Uart::Mini);
        // dtoverlay=uart3: GPIO 4/5, whatever serial0 is.
        QCOMPARE(s3UartOf(tree(dir, "/soc/serial@7e215040", "okay")), BootPlan::S3Uart::Uart3);
        QCOMPARE(s3UartOf(tree(dir, "/soc/serial@7e201000", "okay")), BootPlan::S3Uart::Uart3);
    }
};

QTEST_GUILESS_MAIN(TestFirmware)
#include "tst_firmware.moc"
