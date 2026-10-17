// pipeline/source, and what decides from it: what code a checkout holds for a
// deck, by content, on a scratch repository with the deck's paths, a
// submodule and files that are not the deck's; the golden's records; one
// build of a deck at a time; whether an emulated deck came from an older
// golden.

#include "decks/instances.h"
#include "pipeline/imagebuild.h"
#include "pipeline/source.h"
#include "util/fail.h"
#include "util/files.h"
#include "util/filelock.h"
#include "util/process.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

class TestSource : public QObject {
    Q_OBJECT
    QTemporaryDir t;
    QString repo, first;

    static QString git(const QString& dir, const QStringList& args) {
        proc::Options o;
        o.quiet = true;
        const proc::Result r = proc::capture("git", QStringList{"-C", dir, "-c", "user.name=t", "-c", "user.email=t@t",
                                                                "-c", "commit.gpgsign=false", "-c", "protocol.file.allow=always"} +
                                                        args, o);
        if (!r.ok()) qFatal("git %s: %s", qPrintable(args.join(' ')), r.err.constData());
        return r.text();
    }
    void put(const QString& rel, const QByteArray& data) {
        QDir().mkpath(QFileInfo(repo + "/" + rel).absolutePath());
        files::write(repo + "/" + rel, data);
    }
    QString commit(const QString& message) {
        git(repo, {"add", "-A"});
        git(repo, {"commit", "-q", "-m", message});
        return git(repo, {"rev-parse", "HEAD"});
    }

private slots:
    void initTestCase() {
        QVERIFY(t.isValid());
        qputenv("PI_QEMU_CACHE", QFile::encodeName(t.filePath("cache")));
        qputenv("PI_QEMU_INSTANCES", QFile::encodeName(t.filePath("instances")));
        const QString sub = t.filePath("ttymidi");
        QDir().mkpath(sub);
        git(sub, {"init", "-q", "-b", "main"});
        files::write(sub + "/ttymidi.c", "int main;\n");
        git(sub, {"add", "-A"});
        git(sub, {"commit", "-q", "-m", "one"});
        repo = t.filePath("repo");
        QDir().mkpath(repo);
        git(repo, {"init", "-q", "-b", "main"});
        put("pi_config/a.service", "a\n");
        put("pi-qemu/deploy/001_x.sh", "x\n");
        put("website/index.html", "w\n");
        put(".gitignore", "doom/wad/\n");
        git(repo, {"submodule", "add", "-q", sub, "mixxx_config/ttymidi"});
        first = commit("first");
    }

    void aCleanCheckoutHoldsItsHead() {
        const source::Record r = source::of(repo);
        QCOMPARE(r.commit, first);
        QVERIFY2(r.clean, qPrintable(r.why));
        QVERIFY(r.known());
        QCOMPARE(r.subject, QString("first"));
        QVERIFY(r.describe().startsWith(first.left(7) + " of "));
    }

    void whatIsNotTheDecksDoesNotCount() {
        put("website/index.html", "changed\n");
        put("website/new.html", "new\n");
        QCOMPARE(source::differs(repo, first), QString());
        const QString website = commit("website only");
        QVERIFY(source::same(repo, first, website));
        QVERIFY(source::of(repo).clean);
    }

    void aChangedFileIsNamed() {
        put("pi_config/a.service", "changed\n");
        QCOMPARE(source::differs(repo, first), QString("pi_config/a.service differs"));
        const source::Record r = source::of(repo);
        QVERIFY(!r.clean);
        QVERIFY(!r.known());
        QVERIFY(r.describe().contains("+ changes (pi_config/a.service differs)"));
        git(repo, {"checkout", "-q", "--", "pi_config/a.service"});
        QCOMPARE(source::differs(repo, first), QString());
    }

    void untrackedFilesCountIgnoredOnesDont() {
        put("pi_config/new.conf", "n\n");
        QCOMPARE(source::differs(repo, first), QString("untracked: pi_config/new.conf"));
        QFile::remove(repo + "/pi_config/new.conf");
        put("doom/wad/doom1.wad", "w"); // fetched, git-ignored
        QCOMPARE(source::differs(repo, first), QString());
    }

    void submodulesMustBeWhereTheCommitPointsThem() {
        const QString sub = repo + "/mixxx_config/ttymidi";
        const QString was = git(sub, {"rev-parse", "HEAD"});
        files::write(sub + "/ttymidi.c", "int main(void);\n");
        QCOMPARE(source::differs(repo, first), QString("mixxx_config/ttymidi has uncommitted changes"));
        git(sub, {"commit", "-q", "-am", "two"});
        const QString two = git(sub, {"rev-parse", "HEAD"});
        QCOMPARE(source::differs(repo, first),
                 QString("mixxx_config/ttymidi is at %1, not %2").arg(two.left(7), was.left(7)));
        // Recorded there, it is another deck.
        const QString bumped = commit("bump ttymidi");
        QVERIFY(!source::same(repo, first, bumped));
        QCOMPARE(source::differs(repo, bumped), QString());
        git(repo, {"reset", "-q", "--hard", first});
        git(sub, {"checkout", "-q", "--detach", was});
        QCOMPARE(source::differs(repo, first), QString());
    }

    void byContentWhateverTheBranch() {
        git(repo, {"checkout", "-q", "-b", "elsewhere"});
        put("website/index.html", "elsewhere\n");
        const QString other = commit("elsewhere's website");
        QCOMPARE(source::differs(repo, first), QString());
        QVERIFY(source::same(repo, first, other));
        put("pi-qemu/deploy/001_x.sh", "y\n");
        const QString deck = commit("a deck change");
        QVERIFY(!source::same(repo, first, deck));
        QCOMPARE(source::differs(repo, first), QString("pi-qemu/deploy/001_x.sh differs"));
        git(repo, {"checkout", "-q", "main"});
        git(repo, {"reset", "-q", "--hard", first});
    }

    void unknownCommitsAreNeverTheSame() {
        QVERIFY(!source::same(repo, first, QString(40, '0')));
        QVERIFY(!source::same(repo, first, {}));
        QVERIFY(source::differs(repo, QString(40, '0')).startsWith("git cannot compare it with 0000000"));
    }

    void recordsGoWholeAndComeBack() {
        source::Record r = source::of(repo);
        r.built = "2026-10-17T11:00:00Z";
        r.card = "/x/trimixxx0.img";
        const QString file = t.filePath("rec.json");
        source::write(file, r);
        QVERIFY(!QFileInfo::exists(file + ".new"));
        const source::Record back = source::read(file);
        QCOMPARE(back.commit, r.commit);
        QCOMPARE(back.clean, true);
        QCOMPARE(back.date, r.date);
        QCOMPARE(back.built, r.built);
        QCOMPARE(back.card, r.card);
        QVERIFY(source::read(t.filePath("none.json")).empty());
        QCOMPARE(source::beside("/c/build/trimixxx0.img"), QString("/c/build/trimixxx0.json"));
        QCOMPARE(source::beside("/c/card.raw"), QString("/c/card.raw.json"));
        source::Record card;
        card.card = "/x/y.img";
        QCOMPARE(card.describe(), QString("/x/y.img"));
        QCOMPARE(source::Record{}.describe(), QString("a card with no record"));
    }

    void goldensCompareByCodeElseByMaking() {
        source::Record a = source::of(repo), b = a;
        a.made = "2026-10-17T10:00:00Z";
        b.made = "2026-10-17T12:00:00Z"; // made again, from the same code
        QVERIFY(source::sameGolden(repo, a, b));
        source::Record x, y; // no commit: only the same making is the same
        x.card = y.card = "/c.img";
        x.made = y.made = "2026-10-17T10:00:00Z";
        QVERIFY(source::sameGolden(repo, x, y));
        y.made = "2026-10-17T12:00:00Z";
        QVERIFY(!source::sameGolden(repo, x, y));
        QVERIFY(!source::sameGolden(repo, a, x));
    }

    void oneBuildOfADeckAtATime() {
        QVERIFY(!imagebuild::running("scratch"));
        {
            const imagebuild::Lock lock("scratch");
            QVERIFY(imagebuild::running("scratch"));
            try {
                const imagebuild::Lock again("scratch");
                QFAIL("a second build of the deck went ahead");
            } catch (const Failure& f) {
                QVERIFY2(f.message.startsWith("a build of scratch is running"), qPrintable(f.message));
            }
            QVERIFY(!imagebuild::running("other"));
        }
        QVERIFY(!imagebuild::running("scratch"));
        QVERIFY(imagebuild::attempt("scratch").state.isEmpty());
    }

    void fileLocksShareReadsAndNotWrites() {
        const QString f = t.filePath("locks/x.lock");
        QVERIFY(!lockedNow(f));
        {
            const FileLock a(f, FileLock::Shared);
            QVERIFY(!lockedNow(f)); // readers share
            QVERIFY(FileLock::tryTake(f, FileLock::Shared).held());
            QVERIFY(!FileLock::tryTake(f, FileLock::Exclusive).held());
        }
        {
            const FileLock w(f, FileLock::Exclusive);
            QVERIFY(lockedNow(f));
            QVERIFY(!FileLock::tryTake(f, FileLock::Shared).held());
        }
        QVERIFY(!lockedNow(f));
    }

    void aDeckFromAnOlderGoldenStartsFresh() {
        Instance i("scratch-deck");
        QDir().mkpath(i.dir());
        source::Record g;
        g.card = "/c/build/trimixxx0.img";
        g.made = "2026-10-17T12:00:00Z";
        // From before origins were kept: older than any golden with a record.
        QVERIFY(i.origin().from.isEmpty());
        QVERIFY(i.behind(g));
        QVERIFY(!i.behind({}));
        // A card of its own never is.
        files::write(i.originFile(), R"({"from": "card", "card": "/x/release.img"})");
        QCOMPARE(i.origin().from, QString("card"));
        QCOMPARE(i.origin().card, QString("/x/release.img"));
        QVERIFY(!i.behind(g));
        // From the golden: the same making, then another.
        files::write(i.originFile(), R"({"from": "golden", "golden": {"card": "/c/build/trimixxx0.img", "made": "2026-10-17T12:00:00Z"}})");
        QVERIFY(!i.behind(g));
        g.made = "2026-10-17T13:00:00Z";
        QVERIFY(i.behind(g));
    }
};

QTEST_GUILESS_MAIN(TestSource)
#include "tst_source.moc"
