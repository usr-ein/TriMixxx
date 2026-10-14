#include "cache/cache.h"

#include "cache/dockeruse.h"
#include "cache/places.h"

#include "board/link.h"
#include "cdj/cdj.h"
#include "util/diskuse.h"
#include "util/fail.h"
#include "util/git.h"
#include "util/paths.h"
#include "util/print.h"
#include "util/process.h"
#include "util/tool.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QRegularExpression>

#include <algorithm>
#include <libproc.h>

namespace cache {

QVector<Kind> kinds() {
    const QString c = paths::cache();
    return {
        {"stock", Shelf::Cache, c + "/stock", "Raspberry Pi OS Lite, downloaded and unpacked",
         "image build (a 0.5 GB download)"},
        {"base", Shelf::Cache, c + "/base", "image build's first boot and base step, per deck", "image build"},
        {"build", Shelf::Cache, c + "/build", "image build's cards, and the builds' logs", "image build"},
        {"golden", Shelf::Cache, c + "/golden", "trimixxx0.img + .state: every deck starts there",
         "deck golden, or image build trimixxx0"},
        {"qemu", Shelf::Cache, c + "/qemu", "the decks' QEMU, patched: its source and build",
         "pi-qemu/build.sh qemu (~10 min)"},
        {"cdj-qemu", Shelf::Cache, c + "/cdj-qemu", "the CDJ emulator's QEMU source, at its pin",
         "cdj build (a download)"},
        {"releases", Shelf::Archive, paths::releases(), "every release built: cards, update bundles",
         "release build, release card"},
        {"cdj-firmware", Shelf::Archive, cdj::firmware(), "the NXS firmware the emulated CDJs boot",
         "cdj firmware C2KNXS.UPD"},
        {"sticks", Shelf::Archive, paths::sticks(), "the decks' USB sticks, and the CDJs' in cdj/",
         "by hand (pi-qemu/README.md)"},
        {"decks", Shelf::Archive, paths::identities(), "each deck's identity: ssh host keys, Wi-Fi",
         "release card, deck prepare"},
        {"captures", Shelf::Archive, paths::captures(), "what each real deck ran before its first card", "deck prepare"},
        {"instances", Shelf::State, paths::instances(), "the emulated decks", "deck rm NAME"},
        {"cdj", Shelf::State, paths::cdjs(), "the emulated CDJs", "cdj rm NAME"},
        {"links", Shelf::State, links::root(), "the links decks and CDJs share", "deck up / cdj up --link"},
    };
}

namespace {

qint64 readPid(const QString& file) {
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll().trimmed().toLongLong() : 0;
}

QStringList cacheKinds() {
    QStringList out;
    for (const Kind& k : kinds())
        if (k.shelf == Shelf::Cache) out << k.name;
    return out;
}

// What a pi-qemu command needs from the cache, by its first words.
QStringList needs(const QStringList& words) {
    const QString verb = words.mid(0, 2).join(' ');
    if (verb == "image build" || verb == "release build") return {"stock", "base", "build", "golden", "qemu"};
    if (verb == "deck golden") return {"build", "golden", "qemu"};
    if (verb == "deck up") return {"golden", "qemu"};
    if (words.value(0) == "run") return {"qemu"};
    if (verb == "cdj build") return {"cdj-qemu"};
    if (verb == "cache prune" || verb == "cache migrate") return cacheKinds();
    return {};
}

QString cwdOf(qint64 pid) {
    proc_vnodepathinfo v{};
    if (::proc_pidinfo(int(pid), PROC_PIDVNODEPATHINFO, 0, &v, sizeof v) != int(sizeof v)) return {};
    return QString::fromUtf8(v.pvi_cdir.vip_path);
}

} // namespace

QVector<User> users() {
    QVector<User> out;
    // The emulated decks, those image build and deck golden run included: a
    // running one's QEMU is the cache's (its card is its own).
    const QDir decks(paths::instances());
    for (const QString& d : decks.entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)) {
        const qint64 pid = readPid(decks.filePath(d + "/pid"));
        if (pid > 0 && proc::name(pid) == kTool) out << User{"deck " + d + " (pid " + QString::number(pid) + ")", {"qemu"}};
    }
    // The emulated CDJs: their firmware is an archive, their QEMU their checkout's.
    for (const QString& n : cdj::Cdj::all())
        if (const qint64 pid = cdj::Cdj(n).pid()) out << User{"CDJ " + n + " (pid " + QString::number(pid) + ")", {}};

    // Every other pi-qemu, build.sh, and anything naming a kind's folder;
    // but what a deck, a CDJ or a pi-qemu listed already runs under it (its
    // QEMU, its emulator).
    proc::Options o;
    o.quiet = true;
    struct Process { qint64 ppid; QString command; };
    QMap<qint64, Process> all;
    for (const QString& line : proc::capture("ps", {"-axww", "-o", "pid=,ppid=,command="}, o).text().split('\n')) {
        const QStringList f = line.trimmed().split(' ', Qt::SkipEmptyParts);
        if (f.size() >= 3) all[f[0].toLongLong()] = {f[1].toLongLong(), f.mid(2).join(' ')};
    }
    QSet<qint64> listed{QCoreApplication::applicationPid()};
    static const QRegularExpression pidOf(R"(\(pid (\d+)\)$)");
    for (const User& u : out) listed << pidOf.match(u.who).captured(1).toLongLong();
    auto under = [&](qint64 pid) {
        for (int depth = 0; depth < 16 && pid > 1; depth++, pid = all.value(pid).ppid)
            if (listed.contains(pid)) return true;
        return false;
    };
    QVector<Kind> watched;
    for (const Kind& k : kinds())
        if (k.shelf == Shelf::Cache) watched << k;
    for (auto p = all.begin(); p != all.end(); ++p) {
        const qint64 pid = p.key();
        if (under(pid)) continue;
        const QStringList words = p->command.split(' ', Qt::SkipEmptyParts);
        if (words.isEmpty()) continue;
        const QString who = (QFileInfo(words[0]).fileName() + " " + words.mid(1).join(' ')).left(80).trimmed() +
                            " (pid " + QString::number(pid) + ")";
        if (QFileInfo(words[0]).fileName() == kTool) {
            out << User{who, needs(words.mid(1))};
            listed << pid;
            continue;
        }
        // build.sh, by its path from where it runs: other projects have theirs.
        const int script = int(words.indexOf(QRegularExpression("(^|/)build\\.sh$")));
        if (script > 0 && QRegularExpression("^(/bin/|/usr/bin/|/opt/homebrew/bin/)?(ba|z)?sh$").match(words[0]).hasMatch() &&
            QFileInfo(QDir(cwdOf(pid)).absoluteFilePath(words[script])).absolutePath().endsWith("/pi-qemu")) {
            out << User{who, {"qemu"}};
            listed << pid;
            continue;
        }
        QStringList named;
        for (const Kind& k : watched)
            if (p->command.contains(k.path)) named << k.name;
        if (!named.isEmpty()) out << User{who, named};
    }
    return out;
}

void prune(const QStringList& names) { prune(names, users()); }

void prune(const QStringList& names, const QVector<User>& using_) {
    const QVector<Kind> all = kinds();
    QVector<Kind> chosen;
    for (const QString& n : names) {
        const auto k = std::find_if(all.begin(), all.end(), [&](const Kind& x) { return x.name == n; });
        if (k == all.end()) fail("no cache " + n + ": " + cacheKinds().join(", "), 2);
        if (k->shelf == Shelf::Archive) fail(n + " is an archive (" + places::shown(k->path) + "): nothing clears it", 2);
        if (k->shelf == Shelf::State) fail(n + " is the decks' and CDJs' own, not a cache (" + places::shown(k->path) + ")", 2);
        chosen << *k;
    }
    // Everything checked before anything goes: a refusal changes nothing.
    QStringList busy;
    for (const User& u : using_)
        for (const Kind& k : chosen)
            if (u.kinds.contains(k.name)) busy << k.name + ": " + u.who;
    if (!busy.isEmpty())
        fail("in use, so nothing is pruned:\n  " + busy.join("\n  ") + "\nOnce they are done, again; or prune the others.");
    const QString archives = QFileInfo(paths::archives()).canonicalFilePath();
    const qint64 before = diskuse::freeBytes(paths::cache());
    for (const Kind& k : chosen) {
        const QFileInfo f(k.path);
        if (!places::there(k.path)) { print() << k.name << ": empty already\n"; continue; }
        // A link is unlinked, never followed; a folder that is really in the
        // archives (a link above it) is left alone.
        const bool link = f.isSymLink();
        if (!link && !archives.isEmpty() && (f.canonicalFilePath() + "/").startsWith(archives + "/"))
            fail(k.path + " is in the archives (" + archives + "): left alone");
        const diskuse::Use u = diskuse::of(k.path);
        const bool gone = link || !f.isDir() ? QFile::remove(k.path) : QDir(k.path).removeRecursively();
        if (!gone) fail("could not remove all of " + k.path);
        print() << k.name << ": removed (" << diskuse::text(u.allocated) << " on disk, " << diskuse::text(u.own)
                << " of its own); " << k.remade << " makes it again\n";
    }
    const qint64 after = diskuse::freeBytes(paths::cache());
    print() << "free on the disk: " << diskuse::text(before) << " before, " << diskuse::text(after) << " now\n";
}

namespace {

// A table of rows: a name, its size on disk and of its own, what it is, and
// what makes it again (or removes it).
QString fit(const QString& s, int width) {
    return (s.size() > width ? s.left(width - 1) + QChar(0x2026) : s).leftJustified(width);
}
QString size(qint64 bytes) { return diskuse::text(bytes).rightJustified(9); }

void head(const QString& title) {
    print() << title << "\n" << "  " << QString(14, ' ') << QString("on disk").rightJustified(9)
            << QString("its own").rightJustified(9) << "\n";
}

void row(const QString& name, const diskuse::Use& u, const QString& what, const QString& how = {}, bool own = true,
         int indent = 2) {
    print() << QString(indent, ' ') << fit(name, 16 - indent) << size(u.allocated)
            << (own ? size(u.own) : QString(9, ' ')) << "  " << (how.isEmpty() ? what : fit(what, 48) + " " + how)
            << "\n";
}

diskuse::Use bytes(qint64 b) {
    diskuse::Use u;
    u.allocated = b;
    return u;
}

// Checkouts: the main one, then each worktree, by `git worktree list`.
QStringList checkouts() {
    QStringList out;
    for (const QString& line : git::tryOut(paths::mainCheckout(), {"worktree", "list", "--porcelain"}).split('\n'))
        if (line.startsWith("worktree ")) out << line.mid(9);
    if (out.isEmpty()) out << paths::mainCheckout();
    return out;
}
QString checkoutName(const QString& checkout) {
    return QDir(checkout) == QDir(paths::mainCheckout()) ? QString("main") : QFileInfo(checkout).fileName();
}

// A checkout's Mixxx build trees in Docker, as mixxx/checkout-id.sh names them.
QString checkoutId(const QString& checkout) {
    if (QDir(checkout) == QDir(paths::mainCheckout())) return {};
    QString name = QFileInfo(checkout).fileName();
    name.replace(QRegularExpression("[^A-Za-z0-9._-]"), "-");
    return "-wt-" + name.left(40) + "-" +
           QString::fromLatin1(QCryptographicHash::hash(checkout.toUtf8(), QCryptographicHash::Sha1).toHex().left(8));
}

// Each emulated deck or CDJ under its kind's row.
void members(const Kind& k) {
    const QStringList names = k.name == "cdj" ? cdj::Cdj::all()
                                              : QDir(k.path).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& n : names) {
        QString state;
        if (k.name == "cdj") {
            state = cdj::Cdj(n).running() ? "running" : "stopped";
        } else {
            const qint64 pid = readPid(k.path + "/" + n + "/pid");
            state = pid > 0 && proc::name(pid) == kTool ? "running"
                    : QFileInfo::exists(k.path + "/" + n + "/state") ? "suspended" : "stopped";
        }
        row(n, diskuse::of(k.path + "/" + n), state, {}, true, 4);
    }
}

void builds() {
    struct Build { QString name, dir, how; };
    const QVector<Build> all{
        {"pi-qemu", "pi-qemu/app/build", "pi-qemu/build.sh app"},
        {"prolink", "mixxx/lib/prolink/target", "cargo clean"},
        {"mixxx", "mixxx/dist", "deck deploy NAME mixxx"},
        {"cdj-emulator", "cdj2000-emulator/build", "cdj build"},
        {"cdj-venv", "cdj2000-emulator/.venv", "cdj build"},
        {"s3", "firmwares/trimixxx-midi/.pio", "pio run"},
    };
    const QStringList trees = checkouts();
    head(QString("\nIn each checkout (%1), where their own tools build them: listed, not moved").arg(trees.size()));
    for (const Build& b : all) {
        diskuse::Use total;
        QVector<QPair<qint64, QString>> where;
        for (const QString& t : trees) {
            const diskuse::Use u = diskuse::of(t + "/" + b.dir);
            if (u.files == 0) continue;
            total += u;
            where.append({u.allocated, checkoutName(t) + " " + diskuse::text(u.allocated)});
        }
        std::sort(where.begin(), where.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
        QStringList parts;
        for (const auto& w : where) parts << w.second;
        row(b.name, total, b.dir, b.how);
        if (!parts.isEmpty()) print() << QString(36, ' ') << fit(parts.join(", "), 80).trimmed() << "\n";
    }
}

// keep: where its reading is kept; none: not kept.
void dockerSection(const QString& keep) {
    const DockerUse d = dockerUse(keep);
    QString when = d.fresh ? QString("read now")
                   : d.at == 0 ? QString("its VM sleeps, and was never read here: again while it runs")
                               : QString("its VM sleeps: as read %1, %2 h ago")
                                     .arg(QDateTime::fromSecsSinceEpoch(d.at).toString("yyyy-MM-dd HH:mm"))
                                     .arg((QDateTime::currentSecsSinceEpoch() - d.at) / 3600);
    head("\nDocker, in its VM (listed only: freeing it is the person's call, docker buildx prune): " + when);
    const QString raw = QDir::homePath() + "/Library/Containers/com.docker.docker/Data/vms/0/data/Docker.raw";
    if (QFileInfo::exists(raw)) row("Docker.raw", diskuse::of(raw), "its VM's disk, on the Mac: what follows, and more");
    if (d.at == 0) return;
    if (d.vmFree >= 0) row("free", bytes(d.vmFree), "in its VM", {}, false);
    // Mixxx's build trees, a checkout's each (mixxx/Dockerfile: mixxx-build[-test]-BASE[CHECKOUT_ID]).
    QHash<QString, QString> byId;
    for (const QString& t : checkouts()) byId[checkoutId(t)] = checkoutName(t);
    // Its build and test trees together, by checkout; a gone worktree's by its name.
    static const QRegularExpression tree(R"(^/mixxx-build(-test)?-.*?(-wt-(.+)-[0-9a-f]{8})?$)");
    QMap<QString, qint64> trees;
    QStringList others;
    qint64 treeBytes = 0, otherBytes = 0, ccache = 0, registry = 0;
    bool gone = false;
    for (const DockerUse::Mount& m : d.mounts) {
        if (const auto t = tree.match(m.id); t.hasMatch()) {
            const QString id = t.captured(2);
            gone |= !byId.contains(id);
            trees[byId.contains(id) ? byId[id] : t.captured(3) + " (gone)"] += m.bytes;
            treeBytes += m.bytes;
        } else if (m.id == "//ccache") {
            ccache += m.bytes;
        } else if (m.id == "//opt/cargo/registry") {
            registry += m.bytes;
        } else {
            others << (m.id.isEmpty() ? QString("?") : m.id.mid(1)) + " " + diskuse::text(m.bytes);
            otherBytes += m.bytes;
        }
    }
    row("build trees", bytes(treeBytes), "Mixxx's, one a checkout",
        gone ? "pi-qemu worktree release; a gone one's: docker buildx prune" : "pi-qemu worktree release", false);
    QStringList perCheckout;
    for (auto t = trees.begin(); t != trees.end(); ++t) perCheckout << t.key() + " " + diskuse::text(t.value());
    if (!perCheckout.isEmpty()) print() << QString(36, ' ') << fit(perCheckout.join(", "), 80).trimmed() << "\n";
    row("ccache", bytes(ccache), "the compiler cache all Mixxx builds share", {}, false);
    row("cargo", bytes(registry), "the cargo registry Mixxx's Rust builds share", {}, false);
    if (!others.isEmpty()) row("other mounts", bytes(otherBytes), others.join(", "), {}, false);
    row("layers", bytes(d.layers), "the build images' layers (" + QString::number(d.records) + " records in all)", {}, false);
    row("contexts", bytes(d.contexts + d.otherRecords), "what builds were sent (COPY . /src's input)", {}, false);
    row("images", bytes(d.images), "docker images", {}, false);
}

} // namespace

void list() {
    // Nothing made here before the move: only shown.
    const bool cacheThere = QFileInfo::exists(paths::cache());
    if (cacheThere) places::linkState(paths::cache());
    const QVector<Kind> all = kinds();
    auto section = [&](Shelf s, const QString& title) {
        head(title);
        for (const Kind& k : all) {
            if (k.shelf != s) continue;
            row(k.name, diskuse::of(k.path), k.what, k.remade);
            if (k.name == "instances" || k.name == "cdj") members(k);
        }
    };
    section(Shelf::Cache, places::shown(paths::cache()) + "/: what pi-qemu makes again; clearing it is fine (" +
                              tool("cache prune KIND... | --all") + ")");
    section(Shelf::Archive, "\n" + places::shown(paths::archives()) + "/: hard to make again; no command clears it");
    section(Shelf::State, "\n~/.pi-qemu/: the decks' and CDJs' own, for their sockets' short paths (linked from " +
                              places::shown(paths::cache()) + "/)");
    builds();
    dockerSection(cacheThere ? paths::cache() + "/docker.json" : QString());

    const QVector<places::Move> waiting = places::planned(paths::mainCheckout(), paths::cdjs(), paths::checkout());
    if (!waiting.isEmpty()) {
        print() << "\nStill where pi-qemu kept them before (" << tool("cache migrate") << " moves them):\n";
        for (const places::Move& m : waiting)
            print() << "  " << places::shown(m.from) << " -> " << places::shown(m.to) << " ("
                    << diskuse::text(diskuse::of(m.from).allocated)
                    << (places::there(m.to) ? QString(", there already: it stays)") : QString(")")) << "\n";
    }
    print() << "\non disk: its blocks, those clones share counted in each (du's figure). its own: what deleting it\n"
               "alone frees. The golden, the build cards and the decks' cards are clones of each other.\n";
}

} // namespace cache
