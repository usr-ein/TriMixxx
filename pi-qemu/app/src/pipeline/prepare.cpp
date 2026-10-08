#include "pipeline/prepare.h"

#include "decks/deck.h"
#include "pipeline/release.h"
#include "util/fail.h"
#include "util/files.h"
#include "util/paths.h"
#include "util/process.h"
#include "util/tool.h"
#include "util/print.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

namespace prepare {

namespace {

void head(const QString& text) { print() << "\n==> " << text << Qt::endl; }

// A command on the deck; its output, trimmed (empty if it failed).
QString ask(Deck& d, const QString& command) {
    proc::Options o;
    o.quiet = true;
    o.timeoutMs = 60000;
    const proc::Result r = d.ssh().capture(command, o);
    return r.ok() ? r.text() : QString();
}

QString indented(const QString& text, const QString& by = "  ") {
    QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    for (QString& l : lines) l.prepend(by);
    return lines.join('\n');
}

// "2025/05/08 ..." as 20250508; 0 if it isn't a date.
int day(const QString& version) {
    static const QRegularExpression date(R"(^(\d{4})/(\d{2})/(\d{2}))");
    const auto m = date.match(version);
    return m.hasMatch() ? (m.captured(1) + m.captured(2) + m.captured(3)).toInt() : 0;
}

constexpr int kTryboot = 20221201; // the first bootloader that knows tryboot_a_b

} // namespace

QString withSerial(const QString& text, const QString& deck, const QString& serial) {
    if (text.trimmed().isEmpty())
        return QString("{\n"
                       "  \"deck\": \"%1\",\n"
                       "  \"about\": \"%1's board, given its own section of the release's config.txt by its serial "
                       "(pi-qemu deck prepare). No S3 wiring of its own here: the canonical one.\",\n"
                       "  \"serial\": \"%2\"\n"
                       "}\n").arg(deck, serial);
    static const QRegularExpression existing(R"x(("serial"\s*:\s*)"[^"]*")x");
    QString t = text;
    if (existing.match(t).hasMatch()) return t.replace(existing, QString("\\1\"%1\"").arg(serial));
    // A new key goes last: after the last value, before the closing brace.
    const int brace = int(t.lastIndexOf('}'));
    if (brace < 0) fail("the unit file is not a JSON object");
    int end = brace;
    while (end > 0 && t[end - 1].isSpace()) end--;
    return t.left(end) + QString(",\n  \"serial\": \"%1\"").arg(serial) + t.mid(end);
}

QStringList configLines(const QString& configTxt) {
    static const QRegularExpression releaseStart(R"(^(# Each deck.s own lines|# ---- release))");
    QStringList out;
    for (QString line : configTxt.split('\n')) {
        if (releaseStart.match(line).hasMatch()) break;
        line = line.section('#', 0, 0).trimmed();
        if (line.isEmpty() || line == "[all]") continue;
        out << line;
    }
    return out;
}

int prepare(Deck& d, const QString& deck, bool eeprom) {
    d.requireUp();
    int status = 0;
    const QString unit = paths::units() + "/" + deck + ".json";
    const QString idDir = paths::identities() + "/" + deck;

    head(d.name() + ", to become " + deck);
    print() << "  " << ask(d, "echo \"runs: $(. /etc/os-release && echo \"$PRETTY_NAME\"), hostname $(hostname), "
                    "$([ -e /etc/rauc/system.conf ] && echo \"a release card ($(sed -n s/^VERSION=//p /etc/trimixxx-release))\" "
                    "|| echo \"a dev card\")\"") << Qt::endl;
    const QString now = d.hostname();
    if (now != deck)
        print() << "  NOTE: its card names it " << deck << ", not " << now << ": " << now
              << ".local stops answering, and so does a router's DHCP reservation made by name (one by MAC address stays)" << Qt::endl;

    // ---- 1. the board's serial ----
    head("serial");
    const QString full = ask(d, "tr -d '\\0' < /proc/device-tree/serial-number 2>/dev/null");
    if (QRegularExpression("^[0-9a-fA-F]{8,16}$").match(full).hasMatch()) {
        const QString serial = "0x" + full.right(8).toLower();
        const QString before = QFileInfo::exists(unit) ? QString::fromUtf8(files::read(unit)) : QString();
        static const QRegularExpression old(R"x("serial"\s*:\s*"([^"]*)")x");
        const QString was = old.match(before).captured(1);
        files::write(unit, withSerial(before, deck, serial).toUtf8());
        print() << "  " << serial << ": "
              << (was == serial ? "unchanged in" : was.isEmpty() ? "written to" : "was " + was + ", now in") << " "
              << QFileInfo(unit).fileName() << Qt::endl;
    } else {
        print() << "  none readable (an emulated deck has none): " << unit << " left as it is" << Qt::endl;
    }

    // ---- 2. its ssh host keys ----
    head("ssh host keys");
    if (!QDir(idDir + "/ssh").entryList({"ssh_host_*_key"}, QDir::Files).isEmpty()) {
        print() << "  " << deck << "'s identity has keys already: kept (pi-qemu/.cache/decks/" << deck << "/ssh)" << Qt::endl;
    } else {
        QDir().mkpath(idDir + "/ssh");
        proc::Options o;
        o.quiet = true;
        const proc::Result tar = d.ssh().capture("cd /etc/ssh && sudo tar -cf - ssh_host_*", o);
        if (!tar.ok() || tar.out.isEmpty()) fail("could not read " + d.name() + "'s ssh host keys");
        proc::Options in;
        in.input = proc::Input::Data;
        in.data = tar.out;
        if (proc::run("tar", {"-C", idDir + "/ssh", "-xf", "-"}, in) != 0) fail("could not unpack the host keys");
        for (const QFileInfo& k : QDir(idDir + "/ssh").entryInfoList({"ssh_host_*_key"}, QDir::Files))
            QFile::setPermissions(k.absoluteFilePath(), QFile::ReadOwner | QFile::WriteOwner);
        print() << "  copied into pi-qemu/.cache/decks/" << deck << "/ssh: the card keeps them" << Qt::endl;
    }
    for (const QFileInfo& k : QDir(idDir + "/ssh").entryInfoList({"ssh_host_*_key.pub"}, QDir::Files))
        print() << "  " << proc::capture("ssh-keygen", {"-lf", k.absoluteFilePath()}).text() << Qt::endl;

    // ---- 3. its bootloader ----
    head("bootloader");
    QString version = ask(d, "vcgencmd bootloader_version 2>/dev/null | head -1");
    if (day(version) > 0) {
        if (day(version) < kTryboot) {
            print() << "  " << version << ": TOO OLD. It would start the fresh card's empty slot B (phase 1, T0)." << Qt::endl;
            if (eeprom) print() << "  --eeprom updates it, below" << Qt::endl;
            else { print() << "  Run this again with --eeprom to update it." << Qt::endl; status = 1; }
        } else {
            print() << "  " << version << ": knows tryboot_a_b" << Qt::endl;
        }
        print() << indented(ask(d, "vcgencmd bootloader_config 2>/dev/null | grep -E '^(BOOT_WATCHDOG|BOOT_ORDER|NET_INSTALL)'")) << Qt::endl;
        // What --eeprom would flash: the newest image this deck's rpi-eeprom
        // package has. A newer one needs the package upgraded on the deck first
        // (sudo apt update && sudo apt install --only-upgrade rpi-eeprom).
        print() << indented(ask(d, "sudo rpi-eeprom-update 2>/dev/null | grep -E '^ *(CURRENT|LATEST):' | head -2 | sed 's/^ *//'"))
              << Qt::endl;
    } else {
        print() << "  none readable (an emulated deck has none)" << Qt::endl;
    }

    // ---- 4. what it runs today, for reference ----
    const QString capture = paths::cache() + "/captures/" + deck + "-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmm");
    head("captured into " + QDir(paths::shared()).relativeFilePath(capture));
    QDir().mkpath(capture);
    QFile::setPermissions(capture, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    proc::Options quiet;
    quiet.quiet = true;
    const proc::Result files = d.ssh().capture(
        "sudo tar -czf - --ignore-failed-read -C / boot/firmware/config.txt boot/firmware/cmdline.txt "
        "etc/NetworkManager/system-connections etc/netplan var/lib/alsa/asound.state 2>/dev/null", quiet);
    files::write(capture + "/files.tgz", files.out, true);
    const proc::Result info = d.ssh().capture(
        "echo \"hostname: $(hostname)\"; ip -br link | grep -v '^lo'; echo; vcgencmd bootloader_version 2>/dev/null; "
        "sudo rpi-eeprom-config 2>/dev/null; echo; apt-mark showmanual 2>/dev/null", quiet);
    files::write(capture + "/info.txt", info.out, true);
    const QStringList listed = proc::capture("tar", {"-tzf", capture + "/files.tgz"}, quiet).text().split('\n', Qt::SkipEmptyParts);
    print() << "  " << listed.filter(QRegularExpression("[^/]$")).size()
          << " files, and info.txt (MACs, EEPROM config, packages)" << Qt::endl;
    // Its own config.txt lines: what it has that a release's config.txt
    // (Pi OS's, with the deck lines every card gets) doesn't, from the newest
    // release built. Up to the lines a release adds, on both sides: a deck may
    // run a release card already.
    print() << "  its own config.txt lines, for its unit file (a panel's dtoverlay: panelOverlay; others: configTxt):" << Qt::endl;
    const QString config = proc::capture("tar", {"-xzOf", capture + "/files.tgz", "boot/firmware/config.txt"}, quiet).text();
    files::write(capture + "/config.txt", config.toUtf8(), true);
    QFileInfoList releases;
    for (const QFileInfo& v : QDir(paths::releases()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (QFileInfo::exists(v.absoluteFilePath() + "/boot.vfat")) releases << QFileInfo(v.absoluteFilePath() + "/boot.vfat");
    std::sort(releases.begin(), releases.end(), [](const QFileInfo& a, const QFileInfo& b) { return a.lastModified() > b.lastModified(); });
    if (!config.isEmpty() && !releases.isEmpty()) {
        proc::Options m = quiet;
        m.env["MTOOLS_SKIP_CHECK"] = "1";
        const QStringList base = configLines(proc::capture("mtype", {"-i", releases.first().absoluteFilePath(), "::/config.txt"}, m).text());
        const QSet<QString> known(base.begin(), base.end());
        QStringList own;
        for (const QString& l : configLines(config))
            if (!known.contains(l)) own << l;
        print() << (own.isEmpty() ? "    none: it starts as every deck does" : indented(own.join('\n'), "    ")) << Qt::endl;
    } else {
        print() << "    (no release built yet to compare with: see config.txt in the capture)" << Qt::endl;
    }

    // ---- --eeprom: the bootloader, updated, with its boot watchdog ----
    if (eeprom) {
        head("the bootloader: updated, with its boot watchdog");
        if (day(version) == 0) fail("no bootloader to update (an emulated deck?)");
        const QString arm = ask(d, "sudo vclog --msg 2>/dev/null | grep -m1 'Starting ARM'");
        const int ms = QRegularExpression(R"(^\s*(\d+)\.)").match(arm).captured(1).toInt();
        if (ms <= 0) fail("no 'Starting ARM' in the firmware's log (vclog --msg): not set");
        if (ms >= 30000) fail(QString("Linux starts %1 s after power-on: too close to 45 s, not set").arg(ms / 1000));
        print() << QString("  Linux starts %1.%2 s after power-on: well within 45 s").arg(ms / 1000).arg(ms / 100 % 10) << Qt::endl;
        const QString old = decks::bootId(d);
        const int rc = d.ssh().run(QStringLiteral(
            "set -e\n"
            "sudo rpi-eeprom-config > /tmp/boot.conf\n"
            "if grep -qx BOOT_WATCHDOG_TIMEOUT=45 /tmp/boot.conf && grep -qx BOOT_WATCHDOG_PARTITION=2 /tmp/boot.conf &&\n"
            "        ! sudo rpi-eeprom-update | grep -q 'UPDATE AVAILABLE'; then\n"
            "    echo '  current, and the watchdog already set'; exit 3\n"
            "fi\n"
            "sed -i '/^BOOT_WATCHDOG_\\(TIMEOUT\\|PARTITION\\)=/d' /tmp/boot.conf\n"
            "printf 'BOOT_WATCHDOG_TIMEOUT=45\\nBOOT_WATCHDOG_PARTITION=2\\n' >> /tmp/boot.conf\n"
            "if [ -e /etc/rauc/system.conf ]; then\n"
            "    # An A/B card: the boot ROM reads recovery.bin from p1, not from the\n"
            "    # slot mounted at /boot/firmware. rpi-eeprom-update then warns that\n"
            "    # p1 holds no .elf: by design (invariant 13).\n"
            "    sudo mount /dev/disk/by-partuuid/5d0bc1ec-01 /mnt\n"
            "    sudo env BOOTFS=/mnt rpi-eeprom-config --apply /tmp/boot.conf\n"
            "    sync; sudo umount /mnt\n"
            "else\n"
            "    sudo rpi-eeprom-config --apply /tmp/boot.conf\n"
            "fi\n"
            "echo '  staged; rebooting to flash it'\n"
            "sudo systemd-run --quiet --on-active=2 systemctl reboot\n"));
        if (rc == 3) return 0;
        if (rc != 0) fail("staging the bootloader update failed");
        QElapsedTimer t;
        t.start();
        for (;;) {
            proc::sleep(5000);
            const QString now = decks::bootId(d, 5);
            if (!now.isEmpty() && now != old) break;
            if (t.elapsed() > 240000) fail("no answer 240 s after the reboot: check the deck");
        }
        print() << "  back after " << t.elapsed() / 1000 << " s" << Qt::endl;
        version = ask(d, "vcgencmd bootloader_version | head -1");
        print() << "  bootloader " << version << Qt::endl;
        const QString config2 = ask(d, "vcgencmd bootloader_config | grep '^BOOT_WATCHDOG'");
        print() << indented(config2, "    ") << Qt::endl;
        if (day(version) < kTryboot || !config2.split('\n').contains("BOOT_WATCHDOG_PARTITION=2"))
            fail("the flash didn't take: check the deck before swapping its card");
        // On an A/B card, the update's files stay on p1: the service that clears
        // them is masked on a release. p1 keeps autoboot.txt alone (invariant 13).
        d.ssh().run("if [ -e /etc/rauc/system.conf ]; then\n"
                    "    sudo mount /dev/disk/by-partuuid/5d0bc1ec-01 /mnt\n"
                    "    sudo rm -f /mnt/recovery.bin /mnt/RECOVERY.[0-9]* /mnt/pieeprom.upd /mnt/pieeprom.sig\n"
                    "    sync; echo \"  p1 tidied: $(ls -A /mnt | tr '\\n' ' ')\"; sudo umount /mnt\n"
                    "fi");
        status = 0;
    }

    head("next");
    print() << "  When every deck is prepared: commit the unit files, tag, build the release,\n"
             "  and make each card (PLAN.md §7):\n"
             "    git add mixxx_config/units && git commit -m \"units: the decks' serials\"\n"
             "    git tag -m \"TriMixxx X.Y.Z\" " << release::kTagPrefix << "X.Y.Z\n"
             "    " << tool("release build") << "\n"
             "    " << tool("release card " + deck) << Qt::endl;
    return status;
}

} // namespace prepare
