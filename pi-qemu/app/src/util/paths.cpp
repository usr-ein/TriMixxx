#include "util/paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace {

QString git(const QString& dir, const QStringList& args) {
    QProcess p;
    p.start("git", QStringList{"-C", dir} + args);
    if (!p.waitForFinished(10000) || p.exitCode() != 0) return {};
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

// The TriMixxx checkout around `dir`, also from inside a submodule (mixxx/,
// mixxx/lib/prolink); empty if there is none.
QString topOf(QString dir) {
    for (int depth = 0; depth < 4 && !dir.isEmpty(); depth++) {
        const QString top = git(dir, {"rev-parse", "--show-toplevel"});
        if (top.isEmpty()) return {};
        if (QFileInfo::exists(top + "/pi-qemu/app/CMakeLists.txt")) return top;
        dir = git(top, {"rev-parse", "--show-superproject-working-tree"});
    }
    return {};
}

} // namespace

namespace paths {

QString checkout() {
    static const QString c = [] {
        QString top = topOf(QDir::currentPath());
        if (top.isEmpty()) top = topOf(QDir::cleanPath(PI_QEMU_SOURCE_DIR)); // where this was built
        return top;
    }();
    return c;
}

QString mainCheckout() {
    static const QString m = [] {
        const QString common = git(checkout(), {"rev-parse", "--path-format=absolute", "--git-common-dir"});
        return common.isEmpty() ? checkout() : QFileInfo(common).absolutePath();
    }();
    return m;
}

bool isWorktree() { return QDir(checkout()) != QDir(mainCheckout()); }

QString piq() { return checkout() + "/pi-qemu"; }
QString units() { return checkout() + "/mixxx_config/units"; }
QString shared() { return mainCheckout() + "/pi-qemu"; }
QString cache() {
    const QString env = qEnvironmentVariable("PI_QEMU_CACHE"); // tests, and builds kept apart
    return env.isEmpty() ? shared() + "/.cache" : env;
}
QString tools() { return shared() + "/qemu/.build/bin"; }
QString golden() { return cache() + "/golden/trimixxx0"; }
QString releases() { return shared() + "/release/out"; }
QString identities() { return cache() + "/decks"; }
QString secretsFile() { return shared() + "/image/secrets.env"; }
QString binary() { return QCoreApplication::applicationFilePath(); }

QString instances() {
    const QString env = qEnvironmentVariable("PI_QEMU_INSTANCES");
    return env.isEmpty() ? QDir::home().filePath(".pi-qemu/instances") : env;
}

QString sshKey() {
    const QString env = qEnvironmentVariable("SSH_KEY");
    if (!env.isEmpty()) return env;
    // Sam's key pair, without a passphrase where this Mac has that copy, or
    // with one (then in the agent: util/sshtarget's requireUsableKey).
    for (const char* k : {".ssh/no_pass/rsa_sam", ".ssh/with_pass/rsa_sam"})
        if (QFileInfo::exists(QDir::home().filePath(k))) return QDir::home().filePath(k);
    return QDir::home().filePath(".ssh/no_pass/rsa_sam");
}

} // namespace paths
