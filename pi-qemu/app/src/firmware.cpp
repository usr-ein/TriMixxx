#include "firmware.h"

#include "card.h"

#include <QDir>
#include <QFile>
#include <QProcess>

namespace {

struct Entry { QString key, value; };

// What the filters test: this start, as the firmware sees it.
struct Start {
    bool tryboot = false;
    int  bootPartition = 0; // the partition config.txt is read from
    int  requested = 0;     // `reboot N`
};

// Filters the firmware evaluates true on a Pi 4 Model B. Anything else ([pi5],
// [cm4], [0x<serial>], ...) is false here, as it is on the deck.
bool filterMatches(const QString& f, const Start& s) {
    if (f == "all" || f == "pi4" || f.startsWith("board-type=0x11")) return true;
    if (f == "tryboot") return s.tryboot;
    if (f.startsWith("boot_partition=")) return f.mid(15).toInt() == s.bootPartition;
    if (f.startsWith("partition=")) return f.mid(10).toInt() == s.requested;
    return false;
}

// config.txt and autoboot.txt: key=value lines under [filters], with include.
// Consecutive filters AND together until [all].
bool parseConfig(Card& card, int part, const QString& name, const Start& start, int depth,
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
            bool m = filterMatches(f, start);
            active = inFilter ? active && m : m;
            inFilter = true;
            continue;
        }
        inFilter = false;
        if (!active) continue;
        if (line.startsWith("include ")) {
            parseConfig(card, part, line.mid(8).trimmed(), start, depth + 1, out);
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

// The boot partition, as a Pi 4's bootloader picks it. Measured on trimixxx3
// with bootloader 2026-05-17 (pi-qemu/PLAN.md, phase 1):
//  - a partition asked for by `reboot N` wins, then autoboot.txt's
//    boot_partition: [all], or [tryboot] on a trial (T1-T3);
//  - with no partition named (autoboot.txt missing, empty or garbled), the
//    first FAT partition holding a start.elf, the documentation's test (T6a,
//    T6c, T7), and nothing starts if that one has no start4.elf (T6b);
//  - a named partition without start4.elf is passed over for the next one
//    with it, wrapping after 8 (PARTITION_WALK, on by default: documented).
static int choosePartition(Card& card, const FirmwareOptions& o, bool* tryboot_a_b, QString* error) {
    int named = o.requestedPartition;
    *tryboot_a_b = false;
    QList<Entry> autoboot;
    if (parseConfig(card, 1, "autoboot.txt", Start{o.tryboot, 0, o.requestedPartition}, 0, &autoboot))
        for (const auto& e : autoboot) {
            if (e.key == "tryboot_a_b") *tryboot_a_b = e.value == "1";
            if (e.key == "boot_partition" && !o.requestedPartition && e.value.toInt() > 0)
                named = e.value.toInt();
        }
    auto bootable = [&](int p) { return card.exists(p, "start4.elf"); };
    if (named > 0) {
        for (int i = 0; i < 8; i++) {
            const int p = (named - 1 + i) % 8 + 1;
            if (bootable(p)) return p;
        }
        *error = QString("no partition from %1 on holds start4.elf: nothing starts").arg(named);
        return 0;
    }
    for (const auto& p : card.partitions()) {
        if (!card.exists(p.number, "start.elf")) continue;
        if (bootable(p.number)) return p.number;
        *error = QString("partition %1 holds start.elf but no start4.elf: a Pi 4 starts nothing "
                         "(its bootloader moves on to USB, then tries again)").arg(p.number);
        return 0;
    }
    *error = "no FAT partition holds start.elf: nothing starts";
    return 0;
}

bool prepareBoot(Card& card, const FirmwareOptions& o, BootPlan* plan, QString* error) {
    bool tryboot_a_b = false;
    plan->partition = choosePartition(card, o, &tryboot_a_b, error);
    if (!plan->partition) return false;
    const int part = plan->partition;
    plan->notes.clear();
    plan->bootWatchdog = 0;

    // A trial without tryboot_a_b=1 is the older, file-level tryboot:
    // tryboot.txt instead of config.txt.
    const Start start{o.tryboot, part, o.requestedPartition};
    QString configName = "config.txt";
    if (o.tryboot && !tryboot_a_b && card.exists(part, "tryboot.txt")) configName = "tryboot.txt";
    QList<Entry> cfg;
    if (!parseConfig(card, part, configName, start, 0, &cfg)) {
        *error = QString("no %1 on partition %2").arg(configName).arg(part);
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
        // The firmware leaves the watchdog running and passes
        // watchdog.open_timeout (T8). kernel_watchdog_partition changed
        // nothing after a hang (T9), so it is ignored here too.
        else if (e.key == "kernel_watchdog_timeout") plan->bootWatchdog = e.value.toInt();
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
    // A kernel= naming a missing file: the firmware says so and loads its
    // default instead (T10).
    if (kernel != "kernel8.img" && !card.exists(part, kernel)) {
        plan->notes << QString("kernel file %1 does not exist: kernel8.img").arg(kernel);
        kernel = "kernel8.img";
    }
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

    // /chosen/bootloader, as bootloader 2026-05-17 leaves it on trimixxx3
    // (phase 1, §2.3's table): which partition started, whether as a trial.
    // rsts read 0x1000 after a reboot, a power-on and a watchdog reset alike.
    const QString bl = "/chosen/bootloader";
    if (!tool("fdtput", {"-c", "-p", plan->dtb, bl}, error)) return false;
    const QList<QPair<const char*, quint32>> cells{
        {"boot-mode", 1}, {"build-timestamp", 0x6a0a134e}, {"capabilities", 0x7f},
        {"partition", quint32(part)}, {"rsts", 0x1000}, {"tryboot", o.tryboot ? 1u : 0u},
        {"update-timestamp", 0x6aa88f02}};
    for (const auto& [name, v] : cells)
        if (!tool("fdtput", {"-t", "u", plan->dtb, bl, name, QString::number(v)}, error)) return false;
    if (!tool("fdtput", {"-t", "s", plan->dtb, bl, "name", "bootloader"}, error) ||
        !tool("fdtput", {"-t", "s", plan->dtb, bl, "version", "224877da90f82a72dbcc9db10bcf059259f54680"}, error))
        return false;

    // cmdline.txt, read fresh every boot: Raspberry Pi OS's first boot changes
    // the card's disk ID and rewrites it. The firmware puts the tree's own
    // bootargs first and resolves console=serial0/1 to the UART the aliases name.
    auto raw = card.readFile(part, cmdlineFile);
    if (!raw) { *error = cmdlineFile + " missing"; return false; }
    QString line = fdtget(plan->dtb, "/chosen", "bootargs") +
                   QString(" bcm2708_fb.fbwidth=%1 bcm2708_fb.fbheight=%2 bcm2708_fb.fbdepth=%3 ")
                       .arg(o.fbWidth).arg(o.fbHeight).arg(o.fbDepth) +
                   (plan->bootWatchdog ? QString("watchdog.open_timeout=%1 ").arg(plan->bootWatchdog) : QString()) +
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
