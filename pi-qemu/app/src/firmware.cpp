#include "firmware.h"

#include "card.h"

#include <QDir>
#include <QFile>
#include <QProcess>

namespace {

struct Entry { QString key, value; };

// Filters the firmware evaluates true on a Pi 4 Model B. Anything else ([pi5],
// [cm4], [0x<serial>], ...) is false here, as it is on the deck.
bool filterMatches(const QString& f, bool tryboot) {
    if (f == "all" || f == "pi4" || f.startsWith("board-type=0x11")) return true;
    if (f == "tryboot") return tryboot;
    return false;
}

// config.txt and autoboot.txt: key=value lines under [filters], with include.
// Consecutive filters AND together until [all].
bool parseConfig(Card& card, int part, const QString& name, bool tryboot, int depth,
                 QList<Entry>* out) {
    if (depth > 8) return false;
    auto data = card.readFile(part, name);
    if (!data) return false;
    bool active = true, inFilter = false;
    for (QString line : QString::fromUtf8(*data).split('\n')) {
        if (int h = line.indexOf('#'); h >= 0) line.truncate(h);
        line = line.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith('[') && line.endsWith(']')) {
            QString f = line.mid(1, line.size() - 2).toLower();
            if (f == "all") { active = true; inFilter = false; continue; }
            bool m = filterMatches(f, tryboot);
            active = inFilter ? active && m : m;
            inFilter = true;
            continue;
        }
        inFilter = false;
        if (!active) continue;
        if (line.startsWith("include ")) {
            parseConfig(card, part, line.mid(8).trimmed(), tryboot, depth + 1, out);
            continue;
        }
        if (line.startsWith("initramfs ")) {
            out->append({"initramfs", line.mid(10).trimmed().section(' ', 0, 0)});
            continue;
        }
        int eq = line.indexOf('=');
        if (eq > 0) out->append({line.left(eq).trimmed(), line.mid(eq + 1).trimmed()});
    }
    return true;
}

// The graphics stack (V3D, HVS, DSI and the panels on it) is not modelled, so
// its overlays are left out and the kernel uses the firmware framebuffer.
bool emulable(const QString& overlay) {
    for (const char* p : {"vc4-kms", "vc4-fkms", "rpi-ft5406", "rpi-backlight"})
        if (overlay.startsWith(p)) return false;
    return !overlay.contains("panel") && !overlay.contains("dsi");
}

bool tool(const QString& program, const QStringList& args, QString* error, QByteArray* out = nullptr) {
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(30000) || p.exitCode() != 0) {
        if (error)
            *error = program + " " + args.join(' ') + ": " +
                     QString::fromUtf8(p.readAllStandardError()).trimmed();
        return false;
    }
    if (out) *out = p.readAllStandardOutput();
    return true;
}

QString fdtget(const QString& dtb, const QString& node, const QString& prop) {
    QByteArray out;
    tool("fdtget", {dtb, node, prop}, nullptr, &out);
    return QString::fromUtf8(out).trimmed();
}

} // namespace

bool prepareBoot(Card& card, const FirmwareOptions& o, BootPlan* plan, QString* error) {
    // autoboot.txt on partition 1 picks the boot partition, as on a Pi 4.
    plan->partition = 0;
    QList<Entry> autoboot;
    if (parseConfig(card, 1, "autoboot.txt", o.tryboot, 0, &autoboot))
        for (const auto& e : autoboot)
            if (e.key == "boot_partition" && e.value.toInt() > 0) plan->partition = e.value.toInt();
    if (!plan->partition) plan->partition = card.firstFat();
    if (!plan->partition) { *error = "no FAT partition to boot from"; return false; }
    const int part = plan->partition;

    QList<Entry> cfg;
    if (!parseConfig(card, part, "config.txt", o.tryboot, 0, &cfg)) {
        *error = QString("no config.txt on partition %1").arg(part);
        return false;
    }

    QString kernel = "kernel8.img", initramfs, dt = "bcm2711-rpi-4-b.dtb", cmdlineFile = "cmdline.txt";
    bool autoInitramfs = false;
    struct Overlay { QString name; QStringList params; };
    QList<Overlay> overlays{{}}; // [0]: the base tree's own dtparams
    for (const auto& e : cfg) {
        if (e.key == "kernel") kernel = e.value;
        else if (e.key == "initramfs") initramfs = e.value;
        else if (e.key == "auto_initramfs") autoInitramfs = e.value == "1";
        else if (e.key == "device_tree") dt = e.value;
        else if (e.key == "cmdline") cmdlineFile = e.value;
        else if (e.key == "dtparam") overlays.last().params += e.value.split(',');
        else if (e.key == "dtoverlay") {
            if (e.value.isEmpty()) { overlays.append(Overlay{}); continue; } // later dtparams: the base again
            QStringList parts = e.value.split(',');
            overlays.append({parts.takeFirst(), parts});
        }
    }
    if (initramfs.isEmpty() && autoInitramfs)
        initramfs = QString(kernel).replace("kernel", "initramfs").remove(".img");

    QDir run(o.runDir);
    auto extract = [&](const QString& from, const QString& to) {
        auto b = card.readFile(part, from);
        if (!b) { *error = QString("%1 not on partition %2").arg(from).arg(part); return false; }
        QFile f(run.filePath(to));
        return f.open(QIODevice::WriteOnly) && f.write(*b) == b->size();
    };
    plan->kernel = run.filePath("kernel.img");
    if (!extract(kernel, "kernel.img")) return false;
    plan->initramfs.clear();
    if (!initramfs.isEmpty()) {
        plan->initramfs = run.filePath("initramfs");
        if (!extract(initramfs, "initramfs")) return false;
    }

    // The device tree: base, then each overlay in config.txt order.
    plan->dtb = run.filePath("board.dtb");
    if (!extract(dt, "board.dtb")) return false;
    plan->skipped.clear();
    for (const auto& ov : overlays) {
        if (ov.name.isEmpty()) {
            if (!ov.params.isEmpty() &&
                !tool(o.dtmerge, QStringList{plan->dtb, plan->dtb, "-"} + ov.params, error))
                return false;
            continue;
        }
        if (!emulable(ov.name)) { plan->skipped << ov.name; continue; }
        QString dtbo = ov.name + ".dtbo";
        if (!extract("overlays/" + dtbo, dtbo)) return false;
        if (!tool(o.dtmerge, QStringList{plan->dtb, plan->dtb, run.filePath(dtbo)} + ov.params, error))
            return false;
    }

    // What the firmware writes into the tree itself: the MAC, and here the one
    // honest marker that this is not a Pi, so systemd-detect-virt says
    // "vm-other" and the units that drive the EEPROM can stand down.
    if (!o.mac.isEmpty() &&
        !tool("fdtput", QStringList{"-t", "bx", plan->dtb, "/scb/ethernet@7d580000", "local-mac-address"} +
                            o.mac.split(':'), error))
        return false;
    tool("fdtput", {"-c", plan->dtb, "/hypervisor"}, nullptr);
    tool("fdtput", {"-t", "s", plan->dtb, "/hypervisor", "compatible", "qemu,raspi4b"}, nullptr);

    // cmdline.txt, read fresh every boot: Raspberry Pi OS's first boot changes
    // the card's disk ID and rewrites it. The firmware puts the tree's own
    // bootargs first and resolves console=serial0/1 to the UART the aliases name.
    auto raw = card.readFile(part, cmdlineFile);
    if (!raw) { *error = cmdlineFile + " missing"; return false; }
    QString line = fdtget(plan->dtb, "/chosen", "bootargs") +
                   QString(" bcm2708_fb.fbwidth=%1 bcm2708_fb.fbheight=%2 bcm2708_fb.fbdepth=%3 ")
                       .arg(o.fbWidth).arg(o.fbHeight).arg(o.fbDepth) +
                   QString::fromUtf8(*raw);
    for (QString n : {"0", "1"}) {
        bool pl011 = fdtget(plan->dtb, "/aliases", "serial" + n).contains("7e201000");
        line.replace("console=serial" + n, pl011 ? "console=ttyAMA0" : "console=ttyS0");
        if (n == "0") plan->s3OnPL011 = pl011;
    }
    // The emulator's own way in: a console on the UART the S3 leaves free (the
    // mini UART, which the deck's config.txt switches off), ahead of the card's
    // console= so tty1 stays /dev/console. A real deck never gets this.
    if (plan->s3OnPL011) {
        QString mini = fdtget(plan->dtb, "/aliases", "serial1");
        if (mini.contains("7e215040") &&
            tool("fdtput", {"-t", "s", plan->dtb, mini, "status", "okay"}, nullptr)) {
            line.prepend("console=ttyS0,115200 ");
            plan->debugConsole = true;
        }
    }
    plan->cmdline = line.simplified();
    return true;
}
