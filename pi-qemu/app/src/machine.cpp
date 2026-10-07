#include "machine.h"

#include "card.h"
#include "firmware.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTimer>

#include <atomic>
#include <sys/types.h>

extern std::atomic<pid_t> g_qemuPid;

// The QOM paths of what QEMU's patched devices expose (qemu/trimixxx-patches.py,
// 18 and 19): the mailbox's reboot flags and the power manager's reset status.
static const char* kPropertyPath = "/machine/soc/peripherals/property";
static const char* kPowerMgtPath = "/machine/soc/peripherals/powermgt";

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
    FirmwareOptions fo;
    fo.tryboot = m_nextTryboot;
    fo.requestedPartition = m_nextPartition;
    fo.mac = m_o.mac;
    fo.fbWidth = m_o.fbWidth, fo.fbHeight = m_o.fbHeight, fo.fbDepth = m_o.fbDepth;
    fo.dtmerge = m_o.dtmerge;
    fo.runDir = m_o.runDir;
    m_nextTryboot = false; // used once, as the firmware uses them
    m_nextPartition = 0;
    BootPlan p;
    if (!prepareBoot(card, fo, &p, error)) return false;
    for (const QString& note : p.notes) emit status(note);
    emit status(QString("booting partition %1%2").arg(p.partition).arg(fo.tryboot ? " (a trial: tryboot)" : ""));
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
        "-monitor", "none",
        // A reset or power-off request pauses QEMU instead: onGuestStopped().
        "-action", "reboot=shutdown,shutdown=pause",
    };
    // kernel_watchdog_timeout: the watchdog left running, as the firmware does.
    if (p.bootWatchdog > 0)
        a << "-global" << QString("bcm2835-powermgt.boot-watchdog=%1").arg(p.bootWatchdog);
    if (!p.initramfs.isEmpty()) a << "-initrd" << p.initramfs;

    // QEMU's first -serial is the PL011, the second the mini UART. The S3 is
    // wired to GPIO 14/15: whichever of them config.txt made serial0.
    a << "-chardev" << (m_o.serialLog.isEmpty()
                            ? "socket,id=s3,path=" + s3Socket() + ",server=on,wait=off"
                            : "file,id=s3,path=" + m_o.serialLog);
    // The other UART: the debug console, when the firmware gave it one --
    // `nc -U run/console.sock` to log in, run/console.log for the boot.
    const QString run = m_o.runDir;
    QString other = "null";
    if (p.debugConsole) {
        QFile::remove(QDir(run).filePath("console.sock"));
        a << "-chardev" << "socket,id=console,server=on,wait=off,path=" + QDir(run).filePath("console.sock") +
                               ",logfile=" + QDir(run).filePath("console.log");
        other = "chardev:console";
    }
    if (p.s3OnPL011) a << "-serial" << "chardev:s3" << "-serial" << other;
    else a << "-serial" << "null" << "-serial" << "chardev:s3";

    if (m_o.audio == "speakers") a << "-audiodev" << "coreaudio,id=snd,in.voices=0";
    else if (m_o.audio.startsWith("wav:")) a << "-audiodev" << "wav,id=snd,path=" + m_o.audio.mid(4);
    else a << "-audiodev" << "none,id=snd"; // silent; the guest still has its sound card
    a << "-device" << "usb-audio,bus=xhci.0,audiodev=snd";

    // eth0 (GENET) is the deck's CDJ port: link-local, nobody on it yet (a
    // cable to an empty switch). The deck reaches home -- ssh, apt -- over
    // wlan0, which the board cannot emulate, so a USB Ethernet adapter on the
    // xHCI takes that role, its MAC one above eth0's.
    a << "-nic" << "none";
    if (m_o.net != "none") {
        QStringList mac = m_o.mac.split(':');
        mac[5] = QString("%1").arg((mac[5].toInt(nullptr, 16) + 1) & 0xff, 2, 16, QChar('0'));
        QString netdev = QString("user,id=home,hostfwd=tcp:127.0.0.1:%1-:22").arg(m_o.sshPort);
        if (m_o.net == "restricted") netdev += ",restrict=on";
        a << "-netdev" << netdev
          << "-device" << "usb-net,bus=xhci.0,netdev=home,mac=" + mac.join(':');
    }
    if (m_o.display == "window") a << "-display" << "cocoa,zoom-to-fit=on";
    else a << "-display" << "none";
    // The first free VNC display from :1, so decks running side by side all get one.
    if (m_o.display != "none") a << "-vnc" << "127.0.0.1:1,to=99";
    for (int i = 0; i < m_o.sticks.size(); i++)
        a << "-drive" << QString("if=none,id=boot%1,format=raw,readonly=on,file=%2").arg(i).arg(m_o.sticks[i])
          << "-device" << QString("usb-storage,bus=xhci.0,drive=boot%1,id=bootstick%1").arg(i);

    // A saved machine instead of a boot: the same machine and devices, its RAM
    // and device state from the file. Only on the first power-on -- a reboot
    // after it reads the card like any other.
    m_restoring = false;
    if (!m_o.restore.isEmpty() && !m_restoreUsed) {
        if (!QFileInfo::exists(m_o.restore)) {
            *error = m_o.restore + ": no such saved machine";
            return false;
        }
        a << "-incoming" << "file:" + QFileInfo(m_o.restore).absoluteFilePath();
        m_restoring = m_restoreUsed = true;
        emit status("restoring " + m_o.restore);
    }

    QFile::remove(QDir(m_o.runDir).filePath("qmp.sock"));
    m_shutdownReason.clear();
    m_savedTo.clear();
    m_qmpReady = false;
    m_startMs = QDateTime::currentMSecsSinceEpoch();
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
    g_qemuPid = pid_t(m_proc.processId());
    QTimer::singleShot(200, this, &Machine::connectQmp);
    return true;
}

void Machine::pullPlug() {
    m_nextTryboot = false;
    m_nextPartition = 0;
    if (running()) m_proc.kill();
}

// The guest asked for a reset (a reboot, a watchdog) or a power-off, and QEMU
// paused. For a reset, read what the firmware keeps across it -- the reboot
// flags (bit 0: tryboot) and the partition in the reset status (`reboot N`:
// partition bit i in RSTS bit 2i; 63 is a halt) -- then quit; onFinished()
// powers on again with them.
void Machine::onGuestStopped() {
    if (m_shutdownReason == "guest-shutdown") { qmp("quit"); return; }
    if (m_shutdownReason != "guest-reset") return;
    qmp("qom-get", QJsonObject{{"path", kPropertyPath}, {"property", "reboot-flags"}},
        [this](const QJsonObject& r) {
            const quint32 flags = quint32(r["return"].toDouble());
            qmp("qom-get", QJsonObject{{"path", kPowerMgtPath}, {"property", "rsts"}},
                [this, flags](const QJsonObject& r) {
                    const quint32 rsts = quint32(r["return"].toDouble());
                    int part = 0;
                    for (int i = 0; i < 6; i++) part |= int((rsts >> (2 * i)) & 1) << i;
                    m_nextTryboot = flags & 1;
                    m_nextPartition = part == 63 ? 0 : part;
                    qmp("quit");
                });
        });
}

void Machine::onFinished() {
    g_qemuPid = 0;
    m_qmp.abort();
    while (!m_pending.isEmpty()) m_pending.dequeue();
    if (m_stopping) return;
    if (!m_savedTo.isEmpty()) {
        emit status("saved to " + m_savedTo + "; the Pi is off");
    } else if (m_shutdownReason == "guest-reset") {
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
    m_pending.enqueue([this](const QJsonObject&) {
        m_qmpReady = true;
        if (m_restoring) finishRestore(0);
    });
}

// The saved machine loads paused (it was saved paused): wait for the load,
// then let it run.
void Machine::finishRestore(int tries) {
    qmp("query-status", {}, [this, tries](const QJsonObject& r) {
        const QString st = r["return"].toObject()["status"].toString();
        if (st == "paused" || st == "running") {
            if (st == "paused") qmp("cont");
            m_restoring = false;
            emit status(QString("restored in %1 s").arg((QDateTime::currentMSecsSinceEpoch() - m_startMs) / 1000.0, 0, 'f', 1));
        } else if (tries < 600 && running()) {
            QTimer::singleShot(100, this, [this, tries] { finishRestore(tries + 1); });
        } else if (running()) {
            emit status("the saved machine did not load (" + st + "): see qemu.log");
        }
    });
}

void Machine::save(const QString& file, Done done) {
    if (!running() || !m_qmpReady) { done(false, "the Pi is not running"); return; }
    if (m_restoring) { done(false, "still restoring"); return; }
    const QString path = QFileInfo(file).absoluteFilePath();
    QFile::remove(path);
    qmp("stop", {}, [this, path, done](const QJsonObject& r) {
        if (r.contains("error")) { done(false, r["error"].toObject()["desc"].toString()); return; }
        qmp("migrate", QJsonObject{{"uri", "file:" + path}}, [this, path, done](const QJsonObject& r) {
            if (r.contains("error")) {
                qmp("cont");
                done(false, r["error"].toObject()["desc"].toString());
                return;
            }
            pollMigration(0, [this, path, done](bool ok, const QString& msg) {
                if (!ok) { qmp("cont"); done(false, msg); return; }
                m_savedTo = path;
                qmp("quit");
                done(true, "saved to " + path + "; the Pi is off");
            });
        });
    });
}

void Machine::pollMigration(int tries, Done done) {
    qmp("query-migrate", {}, [this, tries, done](const QJsonObject& r) {
        const QJsonObject m = r["return"].toObject();
        const QString st = m["status"].toString();
        if (st == "completed") done(true, {});
        else if (st == "failed" || st == "cancelled") done(false, "saving failed: " + m["error-desc"].toString());
        else if (tries > 1200 || !running()) done(false, "saving did not finish");
        else QTimer::singleShot(100, this, [this, tries, done] { pollMigration(tries + 1, done); });
    });
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
            // The first reason counts: the quit that follows a guest's
            // request reports one of its own.
            if (m["event"].toString() == "SHUTDOWN" && m_shutdownReason.isEmpty()) {
                m_shutdownReason = m["data"].toObject()["reason"].toString();
                onGuestStopped();
            }
            continue;
        }
        if (!m_pending.isEmpty()) m_pending.dequeue()(m);
    }
}
