#include "util/git.h"

#include "util/fail.h"
#include "util/process.h"

#include <QRegularExpression>
#include <QVersionNumber>

namespace git {

QString out(const QString& dir, const QStringList& args) {
    proc::Options o;
    o.quiet = true;
    const proc::Result r = proc::capture("git", QStringList{"-C", dir} + args, o);
    if (!r.ok()) fail("git " + args.join(' ') + ": " + QString::fromUtf8(r.err).trimmed());
    return r.text();
}

QString tryOut(const QString& dir, const QStringList& args) {
    proc::Options o;
    o.quiet = true;
    const proc::Result r = proc::capture("git", QStringList{"-C", dir} + args, o);
    return r.ok() ? r.text() : QString();
}

int run(const QString& dir, const QStringList& args) { return proc::run("git", QStringList{"-C", dir} + args); }

QString newestVersion(const QStringList& tags, const QString& prefix) {
    static const QRegularExpression semver(R"(^\d+\.\d+\.\d+$)");
    QVersionNumber best;
    QString bestText;
    for (const QString& t : tags) {
        if (!t.startsWith(prefix)) continue;
        const QString v = t.mid(prefix.size());
        if (!semver.match(v).hasMatch()) continue;
        const QVersionNumber n = QVersionNumber::fromString(v);
        if (bestText.isEmpty() || n > best) { best = n; bestText = v; }
    }
    return bestText;
}

QString tagVersion(const QString& dir, const QString& prefix) {
    const QString tags = tryOut(dir, {"tag", "--points-at", "HEAD", "--list", prefix + "[0-9]*"});
    return newestVersion(tags.split('\n', Qt::SkipEmptyParts), prefix);
}

bool clean(const QString& dir) { return tryOut(dir, {"status", "--porcelain"}).isEmpty(); }

} // namespace git
