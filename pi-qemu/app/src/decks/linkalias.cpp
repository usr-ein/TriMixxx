#include "decks/linkalias.h"

#include "decks/deck.h"
#include "util/fail.h"
#include "util/files.h"
#include "util/print.h"
#include "util/process.h"
#include "util/tool.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QVector>

#include <cstdio>

namespace linkalias {

namespace {

const QString kBegin = "# >>> pi-qemu deck alias: real decks over their other links (it rewrites this block) >>>";
const QString kEnd = "# <<< pi-qemu deck alias <<<";

// `ssh -G` prints "key value" lines, a key once per value.
QStringList values(const QString& sshG, const QString& key) {
    QStringList found;
    for (const QString& line : sshG.split('\n')) {
        const QString t = line.trimmed();
        if (t.section(' ', 0, 0).toLower() == key) found << t.section(' ', 1);
    }
    return found;
}

proc::Result quietly(const QString& program, const QStringList& args, int timeoutMs = 15000) {
    proc::Options o;
    o.quiet = true;
    o.timeoutMs = timeoutMs;
    return proc::capture(program, args, o);
}

// A Mac device on a link: active, with a link-local address of its own.
bool onALink(const QString& device) {
    const QString s = quietly("ifconfig", {device}).text();
    return s.contains("status: active") && s.contains("inet6 fe80:");
}

// The config written whole, then moved over the old one, its mode kept, and
// the old one kept beside it (.pi-qemu.bak). A symlinked config: its file.
void replaceConfig(const QString& path, const QString& text) {
    const QFileInfo info(path);
    const QString file = info.isSymLink() ? info.symLinkTarget() : path;
    QDir().mkpath(QFileInfo(file).absolutePath());
    const bool existed = QFile::exists(file);
    const QFile::Permissions mode = existed ? QFile::permissions(file) : (QFile::ReadOwner | QFile::WriteOwner);
    if (existed) {
        QFile::remove(file + ".pi-qemu.bak");
        if (!QFile::copy(file, file + ".pi-qemu.bak")) fail("cannot keep a copy of " + file);
    }
    const QString tmp = file + ".pi-qemu.tmp";
    files::write(tmp, text.toUtf8());
    QFile::setPermissions(tmp, mode);
    if (std::rename(QFile::encodeName(tmp).constData(), QFile::encodeName(file).constData()) != 0)
        fail("cannot replace " + file);
}

} // namespace

QString linkLocal(const QString& ipAddr) {
    const QRegularExpressionMatch m = QRegularExpression(R"(inet6 (fe80:[0-9a-f:]+)/)").match(ipAddr);
    return m.hasMatch() ? m.captured(1) : QString();
}

QStringList wiredDevices(const QString& hardwarePorts) {
    static const QRegularExpression notWired("Wi-Fi|AirPort|Bluetooth|Thunderbolt|iPhone|iPad",
                                             QRegularExpression::CaseInsensitiveOption);
    QStringList found;
    QString port;
    for (const QString& line : hardwarePorts.split('\n')) {
        if (line.startsWith("Hardware Port: ")) {
            port = line.mid(15).trimmed();
        } else if (line.startsWith("Device: ")) {
            const QString device = line.mid(8).trimmed();
            if (!device.isEmpty() && !port.contains(notWired)) found << device;
        }
    }
    return found;
}

QString hostKeyAlias(const QString& sshG) {
    const QString own = values(sshG, "hostkeyalias").value(0);
    if (!own.isEmpty() && own != "none") return own;
    const QString host = values(sshG, "hostname").value(0);
    const QString port = values(sshG, "port").value(0, "22");
    return port == "22" ? host : "[" + host + "]:" + port;
}

QStringList keptOptions(const QString& sshG) {
    QStringList lines;
    for (const QString& v : values(sshG, "user")) lines << "User " + v;
    for (const QString& v : values(sshG, "port"))
        if (v != "22") lines << "Port " + v;
    for (const QString& v : values(sshG, "identityfile")) lines << "IdentityFile " + v;
    for (const QString& v : values(sshG, "identitiesonly"))
        if (v == "yes") lines << "IdentitiesOnly yes";
    for (const QString& v : values(sshG, "forwardagent"))
        if (v != "no") lines << "ForwardAgent " + v;
    for (const QString& v : values(sshG, "connecttimeout"))
        if (v != "none") lines << "ConnectTimeout " + v;
    return lines;
}

QString withHost(const QString& config, const QString& host, const QStringList& lines) {
    QStringList all = config.split('\n');
    if (!all.isEmpty() && all.last().isEmpty()) all.removeLast();
    const qsizetype begin = all.indexOf(kBegin), end = all.indexOf(kEnd);
    QStringList head = all, tail;
    QVector<QStringList> blocks; // the marked block's, one per Host line
    if (begin >= 0 && end > begin) {
        head = all.mid(0, begin);
        tail = all.mid(end + 1);
        for (const QString& line : all.mid(begin + 1, end - begin - 1)) {
            if (blocks.isEmpty() || line.trimmed().startsWith("Host ", Qt::CaseInsensitive)) blocks.push_back(QStringList{});
            blocks.last() << line;
        }
    }
    QStringList mine{"Host " + host};
    for (const QString& l : lines) mine << "    " + l;
    bool replaced = false;
    for (QStringList& b : blocks) {
        if (b.value(0).trimmed().compare("Host " + host, Qt::CaseInsensitive) == 0) {
            b = mine;
            replaced = true;
        }
    }
    if (!replaced) blocks.push_back(mine);

    while (!head.isEmpty() && head.last().trimmed().isEmpty()) head.removeLast();
    QStringList out = head;
    if (!out.isEmpty()) out << "";
    out << kBegin;
    bool first = true;
    for (QStringList b : blocks) {
        while (!b.isEmpty() && b.last().trimmed().isEmpty()) b.removeLast();
        if (b.isEmpty()) continue;
        if (!first) out << "";
        out << b;
        first = false;
    }
    out << kEnd << tail;
    return out.join('\n') + '\n';
}

QString writeEthernet(Deck& d, const QString& config) {
    if (d.emulated()) fail("an emulated deck's link is its instance's (deck up --link): this is for --host ALIAS", 2);
    const QString alias = d.name(), eth = alias + "-eth";
    const QString path = config.isEmpty() ? QDir::homePath() + "/.ssh/config" : config;
    d.requireUp();

    // The deck's side: eth0 cabled, and its link-local address there.
    const proc::Result r = d.ssh().capture("cat /sys/class/net/eth0/carrier; ip -6 -o addr show dev eth0 scope link");
    if (!r.ok()) fail("cannot read " + alias + "'s eth0");
    if (r.text().section('\n', 0, 0).trimmed() != "1")
        fail(alias + "'s eth0 has no link: is its cable in, and the switch on?");
    const QString address = linkLocal(r.text());
    if (address.isEmpty()) fail(alias + "'s eth0 has no IPv6 link-local address");

    // The Mac's side: the wired interface on which the same machine answers
    // there (its boot id), known by the key it has over its usual alias.
    const QString boot = decks::bootId(d);
    if (boot.isEmpty()) fail("cannot reach " + alias);
    const QString sshG = quietly("ssh", {"-G", alias}).text();
    const QString keyAlias = hostKeyAlias(sshG);
    QString device;
    for (const QString& dev : wiredDevices(quietly("networksetup", {"-listallhardwareports"}).text())) {
        if (!onALink(dev)) continue;
        // ssh expands HostName: %% is the address's own %.
        const QStringList via{"-o", "ConnectTimeout=3", "-o", "BatchMode=yes", "-o", "HostName=" + address + "%%" + dev,
                              "-o", "HostKeyAlias=" + keyAlias};
        if (quietly("ssh", d.ssh().args(via, "cat /proc/sys/kernel/random/boot_id")).text() == boot) {
            device = dev;
            break;
        }
    }
    if (device.isEmpty())
        fail("no wired interface of the Mac reaches " + alias + " at " + address + " (Wi-Fi never counts): "
             "is the Mac cabled to the deck's switch?");

    QStringList lines{"# " + alias + " over the Mac's " + device + ", by eth0's link-local address (" + tool("deck alias") + ")",
                      "HostName " + address + "%%" + device, "HostKeyAlias " + keyAlias};
    lines << keptOptions(sshG);
    const QString before = QFile::exists(path) ? QString::fromUtf8(files::read(path)) : QString();
    replaceConfig(path, withHost(before, eth, lines));

    // Through the config, as every verb will reach it; or the old config back.
    const QStringList check{"-o", "ConnectTimeout=5", "-o", "BatchMode=yes", eth, "cat /proc/sys/kernel/random/boot_id"};
    if (quietly("ssh", (config.isEmpty() ? QStringList{} : QStringList{"-F", path}) + check).text() != boot) {
        replaceConfig(path, before);
        fail("wrote " + eth + " into " + path + ", but ssh " + eth + " did not reach " + alias + ": put back as it was");
    }
    print() << eth << ": " << alias << " over the Mac's " << device << " (" << address << "), in " << path
            << " (the previous one: " << QFileInfo(path).fileName() << ".pi-qemu.bak)\n"
            << "  " << tool("deck ssh --host " + eth) << "\n";
    return eth;
}

} // namespace linkalias
