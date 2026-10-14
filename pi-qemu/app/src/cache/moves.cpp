// `pi-qemu cache migrate`: what pi-qemu kept in pi-qemu/.cache,
// pi-qemu/qemu/.build, pi-qemu/release/out and ~/.pi-qemu/cdj, moved into
// .cache/ and archives/ (cache/cache.h), and back.

#include "cache/cache.h"
#include "cache/places.h"

#include "util/diskuse.h"
#include "util/fail.h"
#include "util/files.h"
#include "util/paths.h"
#include "util/print.h"
#include "util/tool.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace cache {

namespace {

using places::Move;

// Where a path is, the links among its folders resolved, not its own: a
// moved link is where it is, whatever it leads to.
QString located(const QString& path) {
    const QFileInfo f(path);
    const QString parent = f.absolutePath();
    if (parent == f.absoluteFilePath()) return parent;
    const QFileInfo p(parent);
    return (p.exists() ? p.canonicalFilePath() : located(parent)) + "/" + f.fileName();
}
bool within(const QString& path, const QString& root) { return (located(path) + "/").startsWith(located(root) + "/"); }

dev_t device(QString path) {
    struct stat st;
    while (::lstat(QFile::encodeName(path).constData(), &st) != 0) path = QFileInfo(path).absolutePath();
    return st.st_dev;
}

void rename(const QString& from, const QString& to) {
    QDir().mkpath(QFileInfo(to).absolutePath());
    if (::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) != 0)
        fail("cannot move " + from + " to " + to + ": " + QString::fromLocal8Bit(std::strerror(errno)));
}

// The journal: each rename done, one JSON line, for --undo.
void record(const QString& journal, const Move& x) {
    QFile f(journal);
    if (!f.open(QIODevice::Append)) fail("cannot write " + journal);
    f.write(QJsonDocument(QJsonObject{{"from", x.from}, {"to", x.to}}).toJson(QJsonDocument::Compact) + "\n");
}

QVector<Move> recorded(const QString& journal) {
    QVector<Move> out;
    QFile f(journal);
    if (!f.open(QIODevice::ReadOnly)) return out;
    for (const QByteArray& line : f.readAll().split('\n')) {
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        if (!o.value("from").toString().isEmpty()) out << Move{o.value("from").toString(), o.value("to").toString()};
    }
    return out;
}

// A folder with nothing in it but Finder's .DS_Store, removed.
void dropIfEmpty(const QString& dir) {
    const QStringList left = QDir(dir).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    if (left == QStringList{".DS_Store"}) QFile::remove(dir + "/.DS_Store");
    QDir().rmdir(dir);
}

struct Outcome {
    QStringList moved, left;
};

// `from` to `to`, a rename. When both are folders, what `from` holds goes in
// one by one; what is in both stays where it was, for the person to look at.
void move(const Move& x, const QString& journal, Outcome& out) {
    if (!places::there(x.to)) {
        const diskuse::Use u = diskuse::of(x.from);
        rename(x.from, x.to);
        record(journal, x);
        out.moved << places::shown(x.from) + " -> " + places::shown(x.to) + " (" + diskuse::text(u.allocated) + ")";
        return;
    }
    const QFileInfo from(x.from), to(x.to);
    if (from.isDir() && !from.isSymLink() && to.isDir() && !to.isSymLink()) {
        for (const QString& e : QDir(x.from).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
            if (e != ".DS_Store") move({x.from + "/" + e, x.to + "/" + e}, journal, out);
        dropIfEmpty(x.from);
        return;
    }
    out.left << places::shown(x.from) + " (" + places::shown(x.to) + " is there already)";
}

// A scratch root's moves (not the main checkout's): only what is under it, both ends.
bool inScope(const Move& x, const QString& root, bool mainRoot) {
    return mainRoot || (within(x.from, root) && within(x.to, root));
}

QVector<Move> confined(QVector<Move> moves, const QString& root, bool mainRoot) {
    moves.erase(std::remove_if(moves.begin(), moves.end(), [&](const Move& x) { return !inScope(x, root, mainRoot); }),
                moves.end());
    return moves;
}

void undo(const QString& root, bool mainRoot) {
    const QString journal = root + "/.cache/moves.json";
    const QVector<Move> done = recorded(journal);
    if (done.isEmpty()) {
        print() << "nothing to move back: no move recorded in " << places::shown(journal) << "\n";
        return;
    }
    QStringList back, left;
    QVector<Move> kept; // what can't go back now stays recorded, for another --undo
    for (auto x = done.rbegin(); x != done.rend(); ++x) {
        if (!inScope(*x, root, mainRoot)) {
            left << places::shown(x->to) + " (outside " + places::shown(root) + ")";
            kept.prepend(*x);
        } else if (places::there(x->to) && !places::there(x->from)) {
            rename(x->to, x->from);
            back << places::shown(x->to) + " -> " + places::shown(x->from);
        } else {
            left << places::shown(x->to) + (places::there(x->to) ? " (" + places::shown(x->from) + " is there again)" : " (gone)");
            if (places::there(x->to)) kept.prepend(*x);
        }
    }
    QFile::remove(journal);
    for (const Move& x : kept) record(journal, x);
    QFile::remove(root + "/.cache/docker.json");
    for (const QString& l : places::stateLinks(root + "/.cache"))
        if (QFileInfo(l).isSymLink()) QFile::remove(l);
    for (const char* d : {"/.cache/stock", "/.cache/cdj-qemu", "/archives/sticks/cdj", "/archives/sticks"}) QDir().rmdir(root + d);
    // The two folders, once nothing but their own .gitignore is left in them.
    for (const char* d : {"/.cache", "/archives"}) {
        const QStringList left =
            QDir(root + d).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        if (left.filter(QRegularExpression("^(?!(\\.gitignore|\\.DS_Store)$)")).isEmpty()) {
            for (const QString& f : left) QFile::remove(root + d + "/" + f);
            QDir().rmdir(root + d);
        }
    }
    print() << "moved back:\n  " << back.join("\n  ") << "\n";
    if (!left.isEmpty()) print() << "left as they are:\n  " << left.join("\n  ") << "\n";
    for (const char* d : {"/.cache", "/archives"})
        if (QFileInfo::exists(root + d))
            print() << places::shown(root + d) << " stays: made since the move ("
                    << QDir(root + d).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).join(", ") << ")\n";
}

} // namespace

void migrate(const QString& root, bool back) { migrate(root, back, users()); }

void migrate(const QString& root, bool back, const QVector<User>& using_) {
    // Everything checked before anything moves: a refusal changes nothing.
    QStringList busy;
    for (const User& u : using_) busy << u.who;
    if (!busy.isEmpty())
        fail("not while these run (what moves is theirs, or under them):\n  " + busy.join("\n  ") +
             "\nStop them first (" + tool("deck stop NAME") + ", " + tool("cdj rm NAME") + "), then again.");
    // A root other than the main checkout, a scratch copy, moves only what is
    // under it: never the CDJs' folder in ~ (the NXS firmware, Sam's sticks),
    // whatever names it, so deleting the copy deletes nothing of his.
    const bool mainRoot = located(root) == located(paths::mainCheckout());
    if (back) return undo(root, mainRoot);
    QString cdjs = paths::cdjs();
    if (!mainRoot && (!within(cdjs, root) || located(cdjs) == located(QDir::home().filePath(".pi-qemu/cdj")))) {
        print() << "not the main checkout: only what is under " << places::shown(root) << " moves; the CDJs' folder, "
                << places::shown(cdjs) << ", stays as it is\n";
        cdjs.clear();
    }
    // A worktree's own QEMU build moves with the main checkout's, run from it;
    // a scratch root is a checkout of its own.
    const QString checkout = mainRoot ? paths::checkout() : root;
    const QVector<Move> moves = confined(places::planned(root, cdjs, checkout), root, mainRoot);
    if (moves.isEmpty()) {
        print() << "nothing to move: " << places::shown(root + "/.cache") << " and " << places::shown(root + "/archives")
                << " have it all\n";
        places::linkState(root + "/.cache");
        return;
    }
    // A rename on one volume is instant, and a clone's blocks stay shared; to
    // another volume it would be a copy of every byte, and clones apart.
    const dev_t here = device(root);
    for (const Move& x : moves)
        if (device(x.from) != here)
            fail(places::shown(x.from) + " is on another volume than " + places::shown(root) +
                 ": moving it would copy it. Move it to " + places::shown(x.to) + " yourself, then again.");
    const qint64 freeBefore = diskuse::freeBytes(root);
    for (const char* d : {"/.cache", "/archives"}) {
        QDir().mkpath(root + d);
        files::ignoreItself(root + d);
    }
    Outcome out;
    for (const Move& x : moves) move(x, root + "/.cache/moves.json", out);
    places::linkState(root + "/.cache");
    // The old folder, emptied: gone, unless something else is left in it.
    dropIfEmpty(root + "/pi-qemu/.cache");
    const QStringList strays = QDir(root + "/pi-qemu/.cache").entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
    const qint64 freeAfter = diskuse::freeBytes(root);
    print() << "moved:\n  " << out.moved.join("\n  ") << "\n";
    if (!out.left.isEmpty()) print() << "left where they were:\n  " << out.left.join("\n  ") << "\n";
    if (!strays.isEmpty())
        print() << "not pi-qemu's to move, left in " << places::shown(root + "/pi-qemu/.cache") << ": "
                << strays.join(", ") << "\n";
    print() << "free on the disk: " << diskuse::text(freeBefore) << " before, " << diskuse::text(freeAfter)
            << " after (a move frees and takes nothing)\n"
            << "moved back by " << tool("cache migrate --undo") << "\n";
}

void requireMoved() {
    QStringList waiting;
    for (const QString& w : places::waiting(paths::mainCheckout(), paths::cdjs())) waiting << places::shown(w);
    if (waiting.isEmpty()) return;
    fail("pi-qemu's caches are still where they were before .cache/ and archives/:\n  " + waiting.join("\n  ") +
         "\n" + tool("cache migrate") + " moves them, once every deck and CDJ is stopped (" + tool("cache") +
         " shows where each goes)");
}

} // namespace cache
