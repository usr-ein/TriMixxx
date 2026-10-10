#include "worktree/worktree.h"

#include "cdj/cdj.h"
#include "util/fail.h"
#include "util/files.h"
#include "util/git.h"
#include "util/paths.h"
#include "util/process.h"
#include "util/print.h"
#include "util/tool.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>

namespace worktree {

namespace {

const QStringList kSubs{"mixxx", "mixxx/lib/prolink", "mixxx_config/ttymidi", "cdj2000-emulator"};

// The emulated CDJ's emulator (cdj/cdj.h): only for those who run CDJs, so a
// branch that does not record it, or a main checkout without it, is no error.
bool optional(const QString& sub) { return sub == "cdj2000-emulator"; }

QString root() { return paths::checkout(); }
QString mainRoot() { return paths::mainCheckout(); }
bool checkedOut(const QString& sub) { return QFileInfo::exists(root() + "/" + sub + "/.git"); }

// This worktree's own git directory (MAIN/.git/worktrees/NAME): where
// `git submodule update` (or add) puts a private clone's, under modules/.
QString ownGitDir() { return QDir(git::out(root(), {"rev-parse", "--absolute-git-dir"})).canonicalPath(); }

// A clone of its own, as `git submodule update` makes, rather than a linked
// worktree of the main checkout's: its gitdir is in this worktree's modules/.
bool privateClone(const QString& sub) {
    const QString gitdir = QDir(git::tryOut(root() + "/" + sub, {"rev-parse", "--absolute-git-dir"})).canonicalPath();
    return !gitdir.isEmpty() && gitdir.startsWith(ownGitDir() + "/modules/");
}

// The commit the repository around `sub` records for it: this worktree's for
// mixxx, ttymidi and the CDJ emulator, mixxx's own for its lib/prolink.
// Empty for an optional one this branch does not record.
QString recorded(const QString& sub) {
    if (sub == "mixxx/lib/prolink") return git::out(root() + "/mixxx", {"rev-parse", "HEAD:lib/prolink"});
    if (optional(sub)) return git::tryOut(root(), {"rev-parse", "HEAD:" + sub});
    return git::out(root(), {"rev-parse", "HEAD:" + sub});
}

bool addSub(const QString& sub) {
    if (checkedOut(sub)) return false;
    const QString sha = recorded(sub);
    if (sha.isEmpty()) return false; // a branch from before it
    const QString borrowed = mainRoot() + "/" + sub;
    if (!QFileInfo::exists(borrowed + "/.git")) {
        if (optional(sub)) {
            print() << sub << ": the main checkout has none to borrow (git -C " << mainRoot()
                    << " submodule update --init " << sub << "), so not here either" << Qt::endl;
            return false;
        }
        fail("the main checkout has no " + sub + " to borrow from: git -C " + mainRoot() +
             " submodule update --init --recursive");
    }
    QDir().rmdir(root() + "/" + sub);
    // Entries left by a worktree removed with --force, before this one is added.
    git::out(borrowed, {"worktree", "prune"});
    git::out(borrowed, {"worktree", "add", "--quiet", "--detach", root() + "/" + sub, sha});
    print() << sub << ": at " << sha.left(10) << ", a worktree of " << borrowed << Qt::endl;
    return true;
}

// Fails, saying why, if removing the linked worktree (or the private clone)
// would lose work.
void checkSub(const QString& sub) {
    if (!checkedOut(sub)) return;
    const QString dir = root() + "/" + sub;
    if (!git::out(dir, {"status", "--porcelain", "--ignore-submodules=all"}).isEmpty())
        fail(sub + " has uncommitted changes: commit them (on a branch) or discard them first");
    if (privateClone(sub)) {
        // Its commits are nowhere else: they have to be on its origin. Not
        // its tags: a mixxx clone holds upstream's, which no branch reaches.
        if (!git::out(dir, {"rev-list", "--max-count=1", "HEAD", "--branches", "--not", "--remotes"}).isEmpty())
            fail(sub + " is a clone of its own, with commits its origin lacks: push them first (git -C " + sub +
                 " push origin BRANCH)");
        // A stash cleans the tree, and its commits are on no branch: it would
        // go with the clone. (A borrowed copy's stash is the main checkout's.)
        if (!git::out(dir, {"stash", "list"}).isEmpty())
            fail(sub + " is a clone of its own with stashed work (git -C " + sub +
                 " stash list): commit it on a branch and push it, or drop it, first");
        return;
    }
    const QString branch = git::tryOut(dir, {"symbolic-ref", "--quiet", "--short", "HEAD"});
    const QString sha = git::out(dir, {"rev-parse", "HEAD"});
    // Detached at a commit nothing else holds: work that would only survive
    // in a reflog. A branch keeps it.
    if (branch.isEmpty() && sha != recorded(sub) &&
        git::tryOut(mainRoot() + "/" + sub, {"branch", "--all", "--contains", sha}).isEmpty())
        fail(sub + " is detached at commits no branch holds: git -C " + sub + " switch -c BRANCH first");
}

void removeSub(const QString& sub) {
    if (!checkedOut(sub)) return;
    if (privateClone(sub)) {
        // Its files go here, its git directory with the rest of modules/ (in
        // release). Not `git submodule deinit`: it edits the .git/config the
        // main checkout shares.
        QDir(root() + "/" + sub).removeRecursively();
        QDir().mkpath(root() + "/" + sub);
        print() << sub << ": released (a clone of its own, all of it pushed)" << Qt::endl;
        return;
    }
    const QString branch = git::tryOut(root() + "/" + sub, {"symbolic-ref", "--quiet", "--short", "HEAD"});
    git::out(mainRoot() + "/" + sub, {"worktree", "remove", root() + "/" + sub});
    QDir().mkpath(root() + "/" + sub);
    print() << sub << ": released" << (branch.isEmpty() ? QString() : " (branch " + branch + " stays in " + mainRoot() + "/" + sub + ")")
          << Qt::endl;
}

// This checkout's Mixxx build trees in Docker's cache, emptied: a throwaway
// build mounts each by its id and deletes what is in it.
void dropBuilds() {
    proc::Options o;
    o.quiet = true;
    o.env["CHECKOUT_ID"] = "";
    const QString id = proc::capture(root() + "/mixxx/checkout-id.sh", {}, o).text();
    if (id.isEmpty()) { print() << "no Docker build trees to drop (the main checkout, or no mixxx)\n"; return; }
    if (!proc::capture("docker", {"info"}, o).ok()) { print() << "Docker is not running: build trees left in place\n"; return; }
    QTemporaryDir tmp;
    files::write(tmp.filePath("Dockerfile"),
                 "FROM debian:trixie\n"
                 "ARG BUILD_ID TEST_ID\n"
                 "RUN --mount=type=cache,target=/b,sharing=locked,id=${BUILD_ID} \\\n"
                 "    --mount=type=cache,target=/t,sharing=locked,id=${TEST_ID} \\\n"
                 "    find /b /t -mindepth 1 -delete\n");
    const int status = proc::run("docker", {"buildx", "build", "--quiet", "--pull=false", "--no-cache", "--platform",
                                            "linux/arm64", "--build-arg", "BUILD_ID=mixxx-build-debian:trixie" + id,
                                            "--build-arg", "TEST_ID=mixxx-build-test-debian:trixie" + id, "--output",
                                            "type=cacheonly", tmp.path()});
    if (status != 0) fail("could not empty the Docker build trees for " + id);
    print() << "Docker build trees for " << id << ": emptied\n";
}

} // namespace

void prepare(bool quiet) {
    if (!paths::isWorktree()) {
        if (!quiet) print() << "this is the main checkout: its submodules are its own\n";
        return;
    }
    bool any = false;
    for (const QString& sub : kSubs) any |= addSub(sub);
    if (!any && !quiet) {
        QStringList out;
        for (const QString& sub : kSubs)
            if (checkedOut(sub)) out << sub;
        print() << "already prepared: " << out.join(", ") << " checked out\n";
    }
}

void status() {
    print() << "checkout: " << root() << (paths::isWorktree() ? "" : " (the main one)") << "\n";
    for (const QString& sub : kSubs) {
        if (!checkedOut(sub)) {
            if (!optional(sub) || !recorded(sub).isEmpty()) print() << "  " << sub.leftJustified(22) << " not checked out\n";
            continue;
        }
        const QString dir = root() + "/" + sub;
        const QString branch = git::tryOut(dir, {"symbolic-ref", "--quiet", "--short", "HEAD"});
        print() << "  " << sub.leftJustified(22) << " " << git::out(dir, {"rev-parse", "--short", "HEAD"}) << " "
              << (branch.isEmpty() ? "(detached)" : branch)
              << (paths::isWorktree() && privateClone(sub) ? " (a clone of its own)" : "") << "\n";
    }
    if (checkedOut("mixxx")) {
        proc::Options o;
        o.quiet = true;
        print() << "  Mixxx build tree: mixxx-build-debian:trixie"
              << proc::capture(root() + "/mixxx/checkout-id.sh", {}, o).text() << "\n";
    }
}

void release() {
    if (!paths::isWorktree()) fail("this is the main checkout: nothing to release");
    // Everything checked before anything is undone: a refusal changes nothing.
    // A CDJ running from this checkout's emulator would run on, unseen, with
    // its files gone.
    for (const QString& n : cdj::Cdj::all()) {
        const cdj::Cdj c(n);
        if (c.running() && c.config().value("checkout").toString() == root())
            fail("CDJ " + n + " runs from this checkout's cdj2000-emulator: " + tool("cdj rm " + n) + " first");
    }
    const QStringList order{"mixxx/lib/prolink", "mixxx", "mixxx_config/ttymidi", "cdj2000-emulator"};
    for (const QString& sub : order) checkSub(sub);
    // git refuses to remove a worktree while its modules/ exists, even empty:
    // it holds private clones' git directories. Only ones this has checked.
    const QString modules = ownGitDir() + "/modules";
    QStringList known;
    for (const QString& sub : order)
        if (checkedOut(sub) && privateClone(sub))
            known << QDir(git::out(root() + "/" + sub, {"rev-parse", "--absolute-git-dir"})).canonicalPath();
    QDirIterator it(modules, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString d = QDir(it.next()).canonicalPath();
        if (std::any_of(known.begin(), known.end(), [&](const QString& k) { return d == k || d.startsWith(k + "/"); }))
            continue;
        const bool gitDir = QFileInfo::exists(d + "/HEAD") && QFileInfo::exists(d + "/objects");
        if (gitDir) fail(modules + " holds a clone this does not know (" + d + "): release it yourself first");
    }
    if (checkedOut("mixxx")) dropBuilds();
    for (const QString& sub : order) removeSub(sub);
    if (QFileInfo::exists(modules) && !QDir(modules).removeRecursively()) fail("cannot remove " + modules);
    print() << "released: git worktree remove " << root() << "\n";
}

} // namespace worktree
