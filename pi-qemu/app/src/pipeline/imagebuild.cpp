#include "pipeline/imagebuild.h"

#include "board/card.h"
#include "decks/emulateddeck.h"
#include "decks/instances.h"
#include "decks/readiness.h"
#include "pipeline/steps.h"
#include "util/buildlog.h"
#include "util/docker.h"
#include "util/envfile.h"
#include "util/fail.h"
#include "util/files.h"
#include "util/paths.h"
#include "util/process.h"
#include "util/tool.h"
#include "util/print.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTextStream>

namespace imagebuild {

namespace {

// Bumped when what the base stage does here changes: it is in the base card's key.
const char* kBaseStage = "base stage 2: seed, first boot, management NIC, 000_base, power off";

QString cacheDir() { return paths::cache(); }

struct Stock { QString name, url, sha256; };

Stock stock() {
    const auto env = envfile::read(paths::piq() + "/image/stock.env");
    return {env.value("STOCK_NAME"), env.value("STOCK_URL"), env.value("STOCK_SHA256")};
}

QString sha256(const QString& file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) fail("cannot read " + file);
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) fail("cannot read " + file);
    return QString::fromLatin1(h.result().toHex());
}

// The stock card, downloaded once, checked, unpacked.
QString stockCard() {
    const Stock s = stock();
    const QString img = cacheDir() + "/" + s.name + ".img";
    if (QFileInfo::exists(img)) return img;
    QDir().mkpath(cacheDir());
    const QString xz = img + ".xz";
    if (!QFileInfo::exists(xz)) {
        const QString part = xz + ".part";
        if (proc::run("curl", {"-fL", "--max-time", "1800", "-o", part, s.url}) != 0) fail("could not download " + s.url);
        const QString got = sha256(part);
        if (got != s.sha256) fail(QString("%1: sha256 %2, not %3 (image/stock.env)").arg(part, got, s.sha256));
        if (!QFile::rename(part, xz)) fail("cannot move " + part);
    }
    if (proc::run("xz", {"-dkT0", xz}) != 0) fail("could not unpack " + xz);
    return img;
}

QString unitField(const QString& deck, const QString& field) {
    const QString unit = paths::units() + "/" + deck + ".json";
    if (!QFileInfo::exists(unit)) return {};
    return QJsonDocument::fromJson(files::read(unit)).object().value(field).toString();
}

// The base card's key: everything that goes into it.
QString baseKey(const QString& deck) {
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData((stock().sha256 + " " + deck + "\n").toUtf8());
    h.addData(files::read(paths::sshKey() + ".pub"));
    h.addData(files::read(paths::piq() + "/deploy/000_base.pi.sh"));
    h.addData(files::read(paths::piq() + "/image/user-data.in"));
    h.addData(files::read(paths::piq() + "/image/network-config"));
    h.addData((secrets::value("SAM1902_PASSWORD") + "\n").toUtf8());
    h.addData(("panelOverlay " + unitField(deck, "panelOverlay") + "\n").toUtf8());
    h.addData(kBaseStage);
    return QString::fromLatin1(h.result().toHex().left(16));
}

// The first-boot seed onto the card's boot partition.
void seed(const QString& card, const QString& deck) {
    QString passwd = "    lock_passwd: true";
    const QString password = secrets::value("SAM1902_PASSWORD");
    if (!password.isEmpty()) {
        proc::Options o;
        o.input = proc::Input::Data;
        o.data = password.toUtf8() + "\n";
        const proc::Result hash = proc::capture("openssl", {"passwd", "-6", "-stdin"}, o);
        if (!hash.ok() || hash.text().isEmpty()) fail("openssl could not hash the console password");
        passwd = "    lock_passwd: false\n    passwd: '" + hash.text() + "'";
    } else {
        QTextStream(stderr) << "no SAM1902_PASSWORD in pi-qemu/image/secrets.env: the console has no password login\n";
    }
    // Filled in outside comments only: a value spread over lines in a comment
    // would leave its later lines bare, and cloud-init would drop the seed.
    QStringList lines = QString::fromUtf8(files::read(paths::piq() + "/image/user-data.in")).split('\n');
    const QString key = QString::fromUtf8(files::read(paths::sshKey() + ".pub")).trimmed();
    for (QString& line : lines)
        if (!line.trimmed().startsWith('#'))
            line.replace("@DECK@", deck).replace("@PASSWD@", passwd).replace("@SSH_KEY@", key);
    const QString userData = lines.join('\n');

    Card c;
    QString err;
    if (!c.open(card, &err) || c.partitions().isEmpty()) fail(card + ": " + (err.isEmpty() ? "no partitions" : err));
    const quint64 offset = c.partitions().first().offset;
    QTemporaryDir tmp;
    files::write(tmp.filePath("user-data"), userData.toUtf8(), true);
    QFile::copy(paths::piq() + "/image/network-config", tmp.filePath("network-config"));
    files::write(tmp.filePath("ssh"), {});
    proc::Options o;
    o.env["MTOOLS_SKIP_CHECK"] = "1";
    for (const QString f : {"user-data", "network-config", "ssh"})
        if (proc::run("mcopy", {"-o", "-i", QString("%1@@%2").arg(card).arg(offset), tmp.filePath(f), "::/" + f}, o) != 0)
            fail("could not write " + f + " onto " + card);
}

// A fresh card's clock starts where its image left it, behind: apt then
// refuses repositories whose signatures are "not live" yet, before NTP has
// caught up. The Mac's clock, given at every boot.
void setClock(Instance& vm) {
    vm.ssh().run(QString("sudo date -u -s @%1 >/dev/null").arg(QDateTime::currentSecsSinceEpoch()));
}

// The emulator's management USB NIC needs a profile of its own: cloud-init
// renders eth0's as match-anything, the USB NIC comes up first and takes it,
// and the system step's eth0 profile would then make it link-local -- no more
// ssh. Binding eth0's profile to eth0 does not last (NetworkManager writes it
// back to netplan as `match: {}`), so the USB NIC gets a DHCP profile of its
// own in /etc/NetworkManager, where netplan does not reach, outranking it on
// usb0 at every boot. Detached: the switch blips the ssh session riding it.
void managementNic(Instance& vm) {
    vm.ssh().run("sudo systemd-run --quiet --unit=pi-qemu-home-nic sh -c \""
                 "nmcli connection add type ethernet ifname usb0 con-name pi-qemu-home ipv4.method auto ipv6.method auto "
                 "connection.autoconnect-priority 100 && nmcli connection up pi-qemu-home\"");
    proc::sleep(8000);
    readiness::waitSsh(vm.ssh(), 120);
    const proc::Result r = vm.ssh().capture("nmcli -t -f NAME,DEVICE connection show");
    print() << r.text() << Qt::endl;
    if (!r.text().split('\n').contains("pi-qemu-home:usb0")) fail("the management NIC did not get its own profile");
}

} // namespace

QString card(const QString& deck) { return cacheDir() + "/build/" + deck + ".img"; }

QStringList plan(const QString& deck) {
    QStringList p{"the stock Raspberry Pi OS card", "seeding cloud-init", "first boot of the stock card", "deploy 000_base",
                  "booting into the deck's boot flags"};
    for (const steps::Step& s : steps::all(paths::piq() + "/deploy"))
        if (s.number != 0) p << "deploy " + s.title();
    p << "sealing";
    if (deck == "trimixxx0") p << "golden snapshot";
    return p;
}

QString build(const QString& deck, const Options& o) {
    // ---- preflight: everything the build needs, before its long first boot ----
    if (!QFileInfo::exists(paths::tools() + "/qemu-system-aarch64")) fail("no QEMU in " + paths::tools() + ": run pi-qemu/build.sh");
    requireUsableKey(paths::sshKey());
    if (proc::capture("sh", {"-c", "command -v mcopy"}).status != 0) fail("mtools missing: brew install mtools");
    docker::requireFree(6);
    const QVector<steps::Step> all = steps::all(paths::piq() + "/deploy");
    if (all.isEmpty() || all.first().number != 0) fail("no 000 deploy step to build the base card with");

    Instance vm("build-" + deck);
    if (vm.exists()) vm.remove(); // a build left behind
    QDir().mkpath(vm.dir());
    // A build that fails leaves its machine off, its card where it is to look at.
    auto off = qScopeGuard([&] {
        if (!vm.running()) return;
        vm.kill();
        QTextStream(stderr) << "the build's machine is off; its card stays for a look: " << tool("deck up " + vm.name())
                            << ", then " << tool("deck rm " + vm.name()) << Qt::endl;
    });
    EmulatedDeck target(vm.name());
    const QString base = cacheDir() + "/base/" + deck + "-" + baseKey(deck) + ".img";

    buildlog::say("the stock Raspberry Pi OS card");
    const QString stockImg = stockCard();
    print() << stockImg << Qt::endl;

    Instance::Up up;
    up.deck = deck;
    if (QFileInfo::exists(base) && !o.rebuildBase) {
        buildlog::say("deploy 000_base: cached, " + QDir(paths::shared()).relativeFilePath(base));
        files::clone(base, vm.card());
        buildlog::say("booting into the deck's boot flags");
        vm.up(up);
        setClock(vm);
    } else {
        files::clone(stockImg, vm.card());
        // QEMU's SD card is a power of two; the first boot grows root into it.
        if (!QFile::resize(vm.card(), 8LL << 30)) fail("cannot grow " + vm.card());
        buildlog::say("seeding cloud-init: user sam1902, hostname " + deck);
        seed(vm.card(), deck);
        buildlog::say("first boot of the stock card (silent, no window): resize, a reboot, cloud-init");
        up.sshWait = 600;
        vm.up(up);
        setClock(vm);
        vm.ssh().run("cloud-init status --wait >/dev/null 2>&1 || true; hostname; uname -r");
        managementNic(vm);
        steps::run(target, {all.first()}, {.rebootBetween = false, .waitReady = false});
        // The boot flags just changed (the PL011 for the S3, no serial console):
        // they apply from the next boot, as on a deck. Powered off rather than
        // rebooted, to keep the card as it is now as the base card.
        buildlog::say("booting into the deck's boot flags (the base card kept)");
        vm.down();
        QDir().mkpath(cacheDir() + "/base");
        for (const QFileInfo& old : QDir(cacheDir() + "/base").entryInfoList({deck + "-*.img"}, QDir::Files))
            QFile::remove(old.absoluteFilePath());
        files::clone(vm.card(), base);
        up.sshWait = 300;
        vm.up(up);
        setClock(vm);
    }
    vm.ssh().run("echo \"serial0 -> $(readlink -f /dev/serial0)\"");

    // ---- the deck: every other step, as against any deck ----
    steps::run(target, all.mid(1));

    // ---- seal ----
    // Cloud-init was the first boot's provisioning: off now (fresh-install.md
    // 1.4), so later boots do not spend seconds in it.
    buildlog::say("sealing: cloud-init off, caches cleared, power off");
    vm.ssh().run("sudo touch /etc/cloud/cloud-init.disabled && sudo apt-get clean");
    vm.down();
    QDir().mkpath(cacheDir() + "/build");
    files::clone(vm.card(), card(deck));
    off.dismiss();
    vm.remove();

    // ---- the golden pair: every agent's instance starts from it ----
    if (deck == "trimixxx0") {
        buildlog::say("golden snapshot: the deck agents' instances start from");
        Instance::golden(card(deck));
    }
    buildlog::say("done: " + card(deck));
    return card(deck);
}

} // namespace imagebuild
