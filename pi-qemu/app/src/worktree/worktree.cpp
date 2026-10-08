#include "worktree/worktree.h"

#include "util/fail.h"
#include "util/files.h"
#include "util/git.h"
#include "util/paths.h"
#include "util/process.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

namespace worktree {

namespace {

const QStringList kSubs{"mixxx", "mixxx/lib/prolink", "mixxx_config/ttymidi"};

QTextStream& out() {
    static QTextStream s(stdout);
    return s;
}

QString root() { return paths::checkout(); }
QString mainRoot() { return paths::mainCheckout(); }
bool checkedOut(const QString& sub) { return QFileInfo::exists(root() + "/" + sub + "/.git"); }

// The commit the repository around `sub` records for it: this worktree's for
// mixxx and ttymidi, mixxx's own for its lib/prolink.
QString recorded(const QString& sub) {
    if (sub == "mixxx/lib/prolink") return git::out(root() + "/mixxx", {"rev-parse", "HEAD:lib/prolink"});
    return git::out(root(), {"rev-parse", "HEAD:" + sub});
}

bool addSub(const QString& sub) {
    if (checkedOut(sub)) return false;
    const QString sha = recorded(sub);
    const QString borrowed = mainRoot() + "/" + sub;
    if (!QFileInfo::exists(borrowed + "/.git"))
        fail("the main checkout has no " + sub + " to borrow from: git -C " + mainRoot() +
             " submodule update --init --recursive");
    QDir().rmdir(root() + "/" + sub);
    // Entries left by a worktree removed with --force, before this one is added.
    git::out(borrowed, {"worktree", "prune"});
    git::out(borrowed, {"worktree", "add", "--quiet", "--detach", root() + "/" + sub, sha});
    out() << sub << ": at " << sha.left(10) << ", a worktree of " << borrowed << Qt::endl;
    return true;
}

// Fails, saying why, if removing the linked worktree would lose work.
void checkSub(const QString& sub) {
    if (!checkedOut(sub)) return;
    const QString dir = root() + "/" + sub;
    if (!git::out(dir, {"status", "--porcelain", "--ignore-submodules=all"}).isEmpty())
        fail(sub + " has uncommitted changes: commit them (on a branch) or discard them first");
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
    const QString branch = git::tryOut(root() + "/" + sub, {"symbolic-ref", "--quiet", "--short", "HEAD"});
    git::out(mainRoot() + "/" + sub, {"worktree", "remove", root() + "/" + sub});
    QDir().mkpath(root() + "/" + sub);
    out() << sub << ": released" << (branch.isEmpty() ? QString() : " (branch " + branch + " stays in " + mainRoot() + "/" + sub + ")")
          << Qt::endl;
}

// This checkout's Mixxx build trees in Docker's cache, emptied: a throwaway
// build mounts each by its id and deletes what is in it.
void dropBuilds() {
    proc::Options o;
    o.quiet = true;
    o.env["CHECKOUT_ID"] = "";
    const QString id = proc::capture(root() + "/mixxx/checkout-id.sh", {}, o).text();
    if (id.isEmpty()) { out() << "no Docker build trees to drop (the main checkout, or no mixxx)\n"; return; }
    if (!proc::capture("docker", {"info"}, o).ok()) { out() << "Docker is not running: build trees left in place\n"; return; }
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
    out() << "Docker build trees for " << id << ": emptied\n";
}

} // namespace

void prepare(bool quiet) {
    if (!paths::isWorktree()) {
        if (!quiet) out() << "this is the main checkout: its submodules are its own\n";
        return;
    }
    bool any = false;
    for (const QString& sub : kSubs) any |= addSub(sub);
    if (!any && !quiet) out() << "already prepared: " << kSubs.join(", ") << " checked out\n";
}

void status() {
    out() << "checkout: " << root() << (paths::isWorktree() ? "" : " (the main one)") << "\n";
    for (const QString& sub : kSubs) {
        if (!checkedOut(sub)) { out() << "  " << sub.leftJustified(22) << " not checked out\n"; continue; }
        const QString dir = root() + "/" + sub;
        const QString branch = git::tryOut(dir, {"symbolic-ref", "--quiet", "--short", "HEAD"});
        out() << "  " << sub.leftJustified(22) << " " << git::out(dir, {"rev-parse", "--short", "HEAD"}) << " "
              << (branch.isEmpty() ? "(detached)" : branch) << "\n";
    }
    if (checkedOut("mixxx")) {
        proc::Options o;
        o.quiet = true;
        out() << "  Mixxx build tree: mixxx-build-debian:trixie"
              << proc::capture(root() + "/mixxx/checkout-id.sh", {}, o).text() << "\n";
    }
}

void release() {
    if (!paths::isWorktree()) fail("this is the main checkout: nothing to release");
    // Everything checked before anything is undone: a refusal changes nothing.
    const QStringList order{"mixxx/lib/prolink", "mixxx", "mixxx_config/ttymidi"};
    for (const QString& sub : order) checkSub(sub);
    if (checkedOut("mixxx")) dropBuilds();
    for (const QString& sub : order) removeSub(sub);
    out() << "released: git worktree remove " << root() << "\n";
}

} // namespace worktree
