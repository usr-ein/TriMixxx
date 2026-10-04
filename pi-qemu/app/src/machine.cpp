#include "machine.h"

#include "card.h"
#include "firmware.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>

Machine::Machine(MachineOptions o, QObject* parent) : QObject(parent), m_o(std::move(o)) {
    connect(&m_proc, &QProcess::finished, this, &Machine::onFinished);
    connect(&m_qmp, &QLocalSocket::readyRead, this, &Machine::onQmpBytes);
}

Machine::~Machine() {
    m_stopping = true;
    if (running()) {
        m_proc.terminate();
        m_proc.waitForFinished(3000);
    }
}

QString Machine::s3Socket() const { return QDir(m_o.runDir).filePath("s3.sock"); }

bool Machine::powerOn(QString* error) {
    Card card;
    if (!card.open(m_o.card, error)) return false;
    // QEMU's SD card model only takes power-of-two sizes.
    if (qint64 n = card.size(); n & (n - 1)) {
        *error = QString("%1 is %2 bytes; an emulated SD card must be a power of two "
                         "(truncate -s 8G %1)").arg(m_o.card).arg(n);
        return false;
    }
    FirmwareOptions fo{false, m_o.mac, m_o.fbWidth, m_o.fbHeight, m_o.fbDepth, m_o.dtmerge, m_o.runDir};
    BootPlan p;
    if (!prepareBoot(card, fo, &p, error)) return false;
    emit status(QString("booting partition %1").arg(p.partition));
    if (!p.skipped.isEmpty())
        emit status("not emulated, left out: " + p.skipped.join(", ") + " (the kernel uses the framebuffer)");

    QStringList a{
        "-M", "raspi4b", "-m", "2G", "-accel", m_o.accel,
        "-kernel", p.kernel, "-dtb", p.dtb, "-append", p.cmdline,
        "-drive", "file=" + m_o.card + ",if=sd,format=raw",
        "-global", QString("bcm2835-fb.xres=%1").arg(m_o.fbWidth),
        "-global", QString("bcm2835-fb.yres=%1").arg(m_o.fbHeight),
        "-global", QString("bcm2835-fb.bpp=%1").arg(m_o.fbDepth),
        // The VL805's place: an xHCI on the PCIe root port. Sound card, sticks,
        // and the tablet that turns clicks in the window into touches.
        "-device", "qemu-xhci,id=xhci,bus=pcie.1,msi=off,msix=off",
        "-device", "usb-tablet,bus=xhci.0", "-device", "usb-kbd,bus=xhci.0",
        "-qmp", "unix:" + QDir(m_o.runDir).filePath("qmp.sock") + ",server=on,wait=off",
        // A second monitor, only for handing QEMU a USB stick's descriptor (add-fd).
        "-qmp", "unix:" + QDir(m_o.runDir).filePath("qmp-fd.sock") + ",server=on,wait=off",
        "-monitor", "none", "-no-reboot",
    };
    if (!p.initramfs.isEmpty()) a << "-initrd" << p.initramfs;

    // QEMU's first -serial is the PL011, the second the mini UART. The S3 is
    // wired to GPIO 14/15: whichever of them config.txt made serial0.
    a << "-chardev" << (m_o.serialLog.isEmpty()
                            ? "socket,id=s3,path=" + s3Socket() + ",server=on,wait=off"
                            : "file,id=s3,path=" + m_o.serialLog);
    if (p.s3OnPL011) a << "-serial" << "chardev:s3" << "-serial" << "null";
    else a << "-serial" << "null" << "-serial" << "chardev:s3";

    if (m_o.audio == "speakers") a << "-audiodev" << "coreaudio,id=snd,in.voices=0";
    else if (m_o.audio.startsWith("wav:")) a << "-audiodev" << "wav,id=snd,path=" + m_o.audio.mid(4);
    else a << "-audiodev" << "none,id=snd"; // silent; the guest still has its sound card
    a << "-device" << "usb-audio,bus=xhci.0,audiodev=snd";

    if (m_o.net == "none") {
        a << "-nic" << "none";
    } else {
        QString nic = QString("user,mac=%1,hostfwd=tcp:127.0.0.1:%2-:22").arg(m_o.mac).arg(m_o.sshPort);
        if (m_o.net == "restricted") nic += ",restrict=on";
        a << "-nic" << nic;
    }
    if (m_o.display == "window") a << "-display" << "cocoa,zoom-to-fit=on";
    else a << "-display" << "none";
    if (m_o.display != "none") a << "-vnc" << "127.0.0.1:1";
    for (int i = 0; i < m_o.sticks.size(); i++)
        a << "-drive" << QString("if=none,id=boot%1,format=raw,readonly=on,file=%2").arg(i).arg(m_o.sticks[i])
          << "-device" << QString("usb-storage,bus=xhci.0,drive=boot%1,id=bootstick%1").arg(i);

    QFile::remove(QDir(m_o.runDir).filePath("qmp.sock"));
    m_shutdownReason.clear();
    m_qmpReady = false;
    m_proc.setProgram(m_o.qemu);
    m_proc.setArguments(a);
    QFile cmd(QDir(m_o.runDir).filePath("qemu.cmd")); // the exact command, to rerun by hand
    if (cmd.open(QIODevice::WriteOnly)) {
        QStringList quoted{m_o.qemu};
        for (const QString& x : a) quoted << (x.contains(' ') ? "'" + x + "'" : x);
        cmd.write(quoted.join(" \\\n  ").toUtf8() + "\n");
    }
    m_proc.setStandardOutputFile(QDir(m_o.runDir).filePath("qemu.log"));
    m_proc.setProcessChannelMode(QProcess::MergedChannels);
    m_proc.start();
    if (!m_proc.waitForStarted(5000)) {
        *error = m_proc.errorString();
        return false;
    }
    QTimer::singleShot(200, this, &Machine::connectQmp);
    return true;
}

void Machine::pullPlug() {
    if (running()) m_proc.kill();
}

void Machine::onFinished() {
    m_qmp.abort();
    while (!m_pending.isEmpty()) m_pending.dequeue();
    if (m_stopping) return;
    if (m_shutdownReason == "guest-reset") {
        emit status("the Pi rebooted; reading the card again");
        QString err;
        if (powerOn(&err)) return;
        emit status("could not boot: " + err);
    } else if (m_shutdownReason == "guest-shutdown") {
        emit status("the Pi powered off");
    } else {
        QFile log(QDir(m_o.runDir).filePath("qemu.log"));
        (void)log.open(QIODevice::ReadOnly); // no log: an empty tail
        emit status(QString("QEMU stopped, exit %1").arg(m_proc.exitCode()) +
                    (m_shutdownReason.isEmpty() ? "" : " (" + m_shutdownReason + ")") +
                    ": " + QString::fromUtf8(log.readAll()).trimmed().right(400));
    }
    emit poweredOff();
}

// ---- QMP ---------------------------------------------------------------------
void Machine::connectQmp() {
    if (!running() || m_qmp.state() != QLocalSocket::UnconnectedState) return;
    m_qmp.connectToServer(QDir(m_o.runDir).filePath("qmp.sock"));
    if (!m_qmp.waitForConnected(200)) {
        QTimer::singleShot(200, this, &Machine::connectQmp);
        return;
    }
    // The greeting comes first; capabilities negotiation is the first command.
    m_qmp.write("{\"execute\":\"qmp_capabilities\"}\n");
    m_pending.enqueue([this](const QJsonObject&) { m_qmpReady = true; });
}

void Machine::qmp(const QString& command, const QJsonObject& args, Reply done) {
    QJsonObject msg{{"execute", command}};
    if (!args.isEmpty()) msg["arguments"] = args;
    if (m_qmp.state() != QLocalSocket::ConnectedState) {
        if (done) done(QJsonObject{{"error", QJsonObject{{"desc", "the Pi is not running"}}}});
        return;
    }
    m_qmp.write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + "\n");
    m_pending.enqueue(done ? done : Reply([](const QJsonObject&) {}));
}

void Machine::onQmpBytes() {
    m_qmpBuf += m_qmp.readAll();
    int nl;
    while ((nl = m_qmpBuf.indexOf('\n')) >= 0) {
        QJsonObject m = QJsonDocument::fromJson(m_qmpBuf.left(nl)).object();
        m_qmpBuf.remove(0, nl + 1);
        if (m.contains("QMP")) continue; // greeting
        if (m.contains("event")) {
            if (m["event"].toString() == "SHUTDOWN")
                m_shutdownReason = m["data"].toObject()["reason"].toString();
            continue;
        }
        if (!m_pending.isEmpty()) m_pending.dequeue()(m);
    }
}
