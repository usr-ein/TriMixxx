// board/firmware: which UART the S3 is on, from a board's device tree; and
// whether a QEMU can give it UART3 (pipeline/imagebuild's qemuHasUart3).

#include "board/firmware.h"
#include "pipeline/imagebuild.h"

#include <QDir>
#include <QFile>
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

// A QEMU build as build.sh leaves one: the binary a symlink into the patched
// tree's build/, the tree's hw/arm/bcm2838_peripherals.c beside it.
QString fakeQemu(const QTemporaryDir& dir, const QString& name, const QByteArray& peripherals) {
    const QString tree = dir.filePath(name);
    QDir().mkpath(tree + "/build");
    QDir().mkpath(tree + "/hw/arm");
    QFile binary(tree + "/build/qemu-system-aarch64");
    if (!binary.open(QIODevice::WriteOnly)) return {};
    binary.close();
    QFile source(tree + "/hw/arm/bcm2838_peripherals.c");
    if (!source.open(QIODevice::WriteOnly)) return {};
    source.write(peripherals);
    source.close();
    QDir().mkpath(tree + "/bin");
    const QString link = tree + "/bin/qemu-system-aarch64";
    QFile::link("../build/qemu-system-aarch64", link);
    return link;
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
    // Patch 22 in the build, whatever else its patches hold: a later patch, or
    // a worktree on another patches file, still has UART3.
    void uart3IsPatch22ItselfNotThisCheckoutsPatches() {
        QTemporaryDir dir;
        QVERIFY(imagebuild::qemuHasUart3(fakeQemu(dir, "with22",
                "    object_initialize_child(obj, \"uart_irq_orgate\", &s->uart_irq_orgate,\n")));
        QVERIFY(!imagebuild::qemuHasUart3(fakeQemu(dir, "before22", "    /* UART0 */\n")));
        // Not a build pi-qemu can read: no UART3 rather than a guess.
        QVERIFY(!imagebuild::qemuHasUart3(dir.filePath("nowhere/qemu-system-aarch64")));
    }
};

QTEST_GUILESS_MAIN(TestFirmware)
#include "tst_firmware.moc"
