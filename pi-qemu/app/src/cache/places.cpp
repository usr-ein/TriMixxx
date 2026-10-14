#include "cache/places.h"

#include "board/link.h"
#include "util/files.h"
#include "util/paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <sys/stat.h>

namespace cache::places {

bool there(const QString& path) {
    struct stat st;
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0;
}

QString shown(const QString& path) {
    const QString root = paths::mainCheckout() + "/", home = QDir::homePath() + "/";
    if (path.startsWith(root)) return path.mid(root.size());
    if (path.startsWith(home)) return "~/" + path.mid(home.size());
    return path;
}

QStringList stateLinks(const QString& cacheDir) {
    return {cacheDir + "/instances", cacheDir + "/cdj", cacheDir + "/links"};
}

void linkState(const QString& cacheDir) {
    const QStringList links = stateLinks(cacheDir);
    const QStringList targets{paths::instances(), paths::cdjs(), links::root()};
    QDir().mkpath(cacheDir);
    files::ignoreItself(cacheDir);
    for (int i = 0; i < links.size(); i++)
        if (!there(links[i])) QFile::link(targets[i], links[i]);
}

QVector<Move> planned(const QString& root, const QString& cdjs, const QString& checkout, bool ours) {
    const QString was = root + "/pi-qemu/.cache", c = root + "/.cache", a = root + "/archives";
    QVector<Move> m;
    for (const char* d : {"base", "build", "golden"}) m << Move{was + "/" + d, c + "/" + d};
    for (const QFileInfo& f : QDir(was).entryInfoList({"*.img", "*.img.xz"}, QDir::Files, QDir::Name))
        m << Move{f.absoluteFilePath(), c + "/stock/" + f.fileName()}; // the stock card, downloaded and unpacked
    for (const char* d : {"decks", "captures", "sticks"}) m << Move{was + "/" + d, a + "/" + d};
    m << Move{root + "/pi-qemu/qemu/.build", c + "/qemu"};
    m << Move{root + "/pi-qemu/release/out", a + "/releases"};
    // The CDJs' folder, if it is to be moved from: their own state stays, for its sockets.
    static const QRegularExpression pinned("^qemu-([0-9a-f]{8})$");
    if (!cdjs.isEmpty()) {
        m << Move{cdjs + "/firmware", a + "/cdj-firmware"};
        for (const QFileInfo& f : QDir(cdjs).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString n = f.fileName(), path = f.absoluteFilePath();
            if (QFileInfo::exists(path + "/cdj.json") || QFileInfo::exists(path + "/pi-qemu.log")) continue; // a CDJ
            if (const auto q = pinned.match(n); q.hasMatch()) m << Move{path, c + "/cdj-qemu/" + q.captured(1)};
            else if (n.startsWith("firmware") || n.startsWith("qemu-")) continue; // the firmware, or cut short
            // Someone's own (Sam's rekordbox copy, sam1/): with the sticks, in a
            // folder of their own, where no deck stick's name finds it (sam1/
            // beside SAM1.stick, his own stick, on a disk that ignores case).
            else if (!ours) m << Move{path, a + "/sticks/cdj/" + n};
        }
    }
    if (QDir(checkout) != QDir(root)) m << Move{checkout + "/pi-qemu/qemu/.build", checkout + "/.cache/qemu"};
    // Only what is there; not an empty folder (Docker makes release/out
    // again as a mount point, each release container).
    m.erase(std::remove_if(m.begin(), m.end(), [](const Move& x) {
                const QFileInfo f(x.from);
                return !there(x.from) ||
                       (f.isDir() && !f.isSymLink() &&
                        QDir(x.from).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)
                            .filter(QRegularExpression("^(?!\\.DS_Store$)")).isEmpty());
            }),
            m.end());
    return m;
}

QStringList waiting(const QString& root, const QString& cdjs) {
    // A worktree's own QEMU build: only build.sh uses it.
    QStringList out;
    for (const Move& x : planned(root, cdjs, root, true))
        if (!there(x.to)) out << x.from;
    return out;
}

} // namespace cache::places
