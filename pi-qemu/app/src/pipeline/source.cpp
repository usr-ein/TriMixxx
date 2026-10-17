#include "pipeline/source.h"

#include "util/fail.h"
#include "util/files.h"
#include "util/process.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>

#include <cstdio>

namespace source {

namespace {

proc::Result git(const QString& dir, const QStringList& args) {
    proc::Options o;
    o.quiet = true;
    return proc::capture("git", QStringList{"-C", dir} + args, o);
}

QStringList lines(const proc::Result& r) { return r.text().split('\n', Qt::SkipEmptyParts); }

// "a, b, c and 4 more"
QString some(const QStringList& names) {
    QString s = names.mid(0, 3).join(", ");
    if (names.size() > 3) s += QString(" and %1 more").arg(names.size() - 3);
    return s;
}

// The deck's submodules, each recorded by the repository it sits in: the
// checkout's own, or mixxx's for its lib/prolink. A parent comes first.
struct Sub {
    QString path;   // in the checkout
    QString parent; // the submodule recording it; empty: the checkout itself
};
const Sub kSubs[] = {{"mixxx", {}}, {"mixxx/lib/prolink", "mixxx"}, {"mixxx_config/ttymidi", {}}};

} // namespace

QStringList deckPaths() {
    return {"pi-qemu/deploy", "pi-qemu/image", "pi_config", "mixxx", "mixxx_config", "trimixxx-launcher", "doom"};
}

QString Record::describe() const {
    if (commit.isEmpty()) return card.isEmpty() ? QString("a card with no record") : card;
    QString s = commit.left(7);
    const QDateTime t = QDateTime::fromString(date, Qt::ISODate);
    if (t.isValid()) s += " of " + t.toLocalTime().toString("yyyy-MM-dd HH:mm");
    if (!clean) s += " + changes (" + (why.isEmpty() ? QString("not recorded") : why) + ")";
    return s;
}

QJsonObject Record::json() const {
    QJsonObject o;
    auto put = [&](const char* key, const QString& v) { if (!v.isEmpty()) o[key] = v; };
    put("commit", commit);
    if (!commit.isEmpty()) o["clean"] = clean;
    put("why", why);
    put("date", date);
    put("subject", subject);
    put("checkout", checkout);
    put("built", built);
    put("made", made);
    put("card", card);
    return o;
}

Record Record::fromJson(const QJsonObject& o) {
    Record r;
    r.commit = o.value("commit").toString();
    r.clean = o.value("clean").toBool();
    r.why = o.value("why").toString();
    r.date = o.value("date").toString();
    r.subject = o.value("subject").toString();
    r.checkout = o.value("checkout").toString();
    r.built = o.value("built").toString();
    r.made = o.value("made").toString();
    r.card = o.value("card").toString();
    return r;
}

Record read(const QString& file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll());
    return d.isObject() ? Record::fromJson(d.object()) : Record{};
}

void write(const QString& file, const Record& r) {
    const QString next = file + ".new";
    files::write(next, QJsonDocument(r.json()).toJson());
    if (std::rename(QFile::encodeName(next).constData(), QFile::encodeName(file).constData()) != 0)
        fail("cannot move " + next + " to " + file);
}

QString beside(const QString& card) { return (card.endsWith(".img") ? card.chopped(4) : card) + ".json"; }

QString differs(const QString& checkout, const QString& commit) {
    QStringList why;
    // Tracked files as the working tree has them, against the commit; the
    // submodules apart, below.
    const proc::Result d = git(checkout, QStringList{"diff", "--name-only", "--ignore-submodules=all", commit, "--"} + deckPaths());
    if (!d.ok()) return "git cannot compare it with " + commit.left(7) + ": " + QString::fromUtf8(d.err).trimmed();
    const QStringList changed = lines(d);
    if (!changed.isEmpty()) why << some(changed) + (changed.size() == 1 ? " differs" : " differ");
    const QStringList untracked = lines(git(checkout, QStringList{"ls-files", "--others", "--exclude-standard", "--"} + deckPaths()));
    if (!untracked.isEmpty()) why << "untracked: " + some(untracked);
    QHash<QString, QString> at; // each submodule's commit, where it matches
    for (const Sub& s : kSubs) {
        const QString parentCommit = s.parent.isEmpty() ? commit : at.value(s.parent);
        if (parentCommit.isEmpty()) continue; // its parent differs already, and said so
        const QString parentDir = s.parent.isEmpty() ? checkout : checkout + "/" + s.parent;
        const QString inParent = s.parent.isEmpty() ? s.path : s.path.mid(s.parent.size() + 1);
        const QString want = git(parentDir, {"rev-parse", "--verify", "--quiet", parentCommit + ":" + inParent}).text();
        if (want.isEmpty()) continue; // not recorded there: not part of that deck
        const QString dir = checkout + "/" + s.path;
        if (!QFileInfo::exists(dir + "/.git")) {
            why << s.path + " is not checked out";
            continue;
        }
        const QString have = git(dir, {"rev-parse", "HEAD"}).text();
        if (have != want) {
            why << s.path + " is at " + have.left(7) + ", not " + want.left(7);
            continue;
        }
        at[s.path] = want;
        if (!git(dir, {"status", "--porcelain", "--ignore-submodules=all"}).text().isEmpty())
            why << s.path + " has uncommitted changes";
    }
    return why.join("; ");
}

bool same(const QString& repo, const QString& a, const QString& b) {
    if (a.isEmpty() || b.isEmpty()) return false;
    if (a == b) return true;
    return git(repo, QStringList{"diff", "--quiet", a, b, "--"} + deckPaths()).status == 0;
}

bool sameGolden(const QString& repo, const Record& a, const Record& b) {
    if (a.known() && b.known()) return same(repo, a.commit, b.commit);
    return a.made == b.made && a.commit == b.commit;
}

Record of(const QString& checkout) {
    Record r;
    r.checkout = checkout;
    r.commit = git(checkout, {"rev-parse", "HEAD"}).text();
    if (r.commit.isEmpty()) {
        r.why = "not a git checkout";
        return r;
    }
    const QStringList log = git(checkout, {"log", "-1", "--format=%cI%n%s", r.commit}).text().split('\n');
    r.date = log.value(0);
    r.subject = log.value(1);
    r.why = differs(checkout, r.commit);
    r.clean = r.why.isEmpty();
    return r;
}

} // namespace source
