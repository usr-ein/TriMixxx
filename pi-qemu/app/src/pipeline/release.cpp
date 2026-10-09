#include "pipeline/release.h"

#include "pipeline/identity.h"
#include "pipeline/imagebuild.h"
#include "util/buildlog.h"
#include "util/docker.h"
#include "util/fail.h"
#include "util/files.h"
#include "util/git.h"
#include "util/paths.h"
#include "util/print.h"
#include "util/process.h"
#include "util/tool.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

namespace release {

namespace {

const char* kSystem = "trimixxx-release";

// The Mac's UTC offset as a POSIX TZ the container's date reads without zone
// files (+0200 is LOC-02:00): its step lines then carry the Mac's time, which
// the build window counts from.
QString macZone() {
    const int offset = int(QDateTime::currentDateTime().offsetFromUtc());
    const int a = qAbs(offset);
    return QString("LOC%1%2:%3").arg(offset >= 0 ? '-' : '+').arg(a / 3600, 2, 10, QChar('0')).arg(a % 3600 / 60, 2, 10, QChar('0'));
}

// The release container, built if it must be, running container.sh VERB.
void container(const QString& verb, const QStringList& extra) {
    docker::requireRunning();
    const QString dir = paths::piq() + "/release";
    proc::Options build;
    build.input = proc::Input::File;
    build.file = dir + "/Dockerfile";
    build.quiet = true;
    const proc::Result image = proc::capture("docker", {"build", "-q", "-t", "trimixxx-release", "-"}, build);
    if (!image.ok()) fail("could not build the release container: " + QString::fromUtf8(image.err).trimmed());
    QDir().mkpath(paths::releases());
    QStringList args{"run", "--rm", "-e", "IN_CONTAINER=1", "-e", "TZ=" + macZone(),
                     "--mount", "type=bind,src=" + dir + ",dst=/release", "-w", "/release",
                     "--mount", "type=bind,src=" + paths::releases() + ",dst=/release/out",
                     "--mount", "type=bind,src=" + paths::checkout() + ",dst=/repo,readonly"};
    args << extra << "trimixxx-release" << "bash" << "container.sh" << verb;
    if (proc::run("docker", args) != 0) fail("the release container's " + verb + " failed");
}

} // namespace

QString version(const QString& given) {
    if (!given.isEmpty()) return given;
    const QString v = git::tagVersion(paths::checkout(), kTagPrefix);
    if (v.isEmpty())
        fail(QString("no version: HEAD has no %1X.Y.Z tag (git tag -m \"TriMixxx X.Y.Z\" %1X.Y.Z), or give --version")
                 .arg(kTagPrefix));
    return v;
}

QString outDir(const QString& version) { return paths::releases() + "/" + version; }

QStringList plan(const BuildOptions& o) {
    QStringList p;
    if (!o.keepSystem) p << imagebuild::plan(kSystem);
    p << "release: the system" << "release: the card" << "release: the update";
    return p;
}

int build(const BuildOptions& o) {
    const QString repo = paths::checkout();
    QString v, commit = git::out(repo, {"describe", "--tags", "--match", QString(kTagPrefix) + "[0-9]*", "--always", "--dirty"});
    // The commit itself, which a tag's name alone doesn't pin down.
    const QString hash = git::out(repo, {"rev-parse", "HEAD"});
    if (!o.rehearsal.isEmpty()) {
        v = o.rehearsal;
        commit += " (rehearsal)";
    } else {
        v = version({});
        if (!git::clean(repo)) fail("the tree has changes: commit them (or --rehearsal VERSION)");
    }
    return buildlog::run(paths::cache() + "/build/" + kSystem + ".log", o.window, plan(o), [&] {
        if (!o.keepSystem) imagebuild::build(kSystem, {});
        if (!QFileInfo::exists(imagebuild::card(kSystem))) fail("no " + QString(kSystem) + " card in pi-qemu/.cache/build");
        container("seal", {"--privileged", "-e", "VERSION=" + v, "-e", "COMMIT=" + commit, "-e", "HASH=" + hash,
                           "--mount", "type=bind,src=" + paths::cache() + "/build,dst=/build,readonly"});
        buildlog::say("release " + v + ": " + outDir(v));
    });
}

void card(const QString& deck, const QString& v) {
    const QString o = outDir(v);
    const QString release = o + "/trimixxx-" + v + ".img";
    if (!QFileInfo::exists(release)) fail("no release " + v + " (" + release + "): " + tool("release build"));
    const QString data = identity::prepare(deck);
    const QString img = o + "/" + deck + "-" + v + ".img";
    files::clone(release, img);
    container("card", {"-e", "DECK=" + deck, "-e", "VERSION=" + v, "--mount", "type=bind,src=" + data + ",dst=/deck,readonly"});
    print() << deck << "'s card: " << img << "\n";
}

void faults(const QString& v) {
    if (!QFileInfo::exists(outDir(v) + "/rootfs.squashfs")) fail("no release " + v + ": " + tool("release build"));
    container("faults", {"-e", "VERSION=" + v});
}

} // namespace release
