// pipeline/golden: when the golden is made again, waited for, or left as it
// is. A scratch main checkout (this test runs in it: paths finds it from the
// working directory), and a stand-in for its image build
// (data/fakebuild.py) that holds the deck's lock and keeps its attempt and
// the golden's record as the real one does: it can move main on while it
// builds, fail, or hang.

#include "pipeline/golden.h"
#include "pipeline/imagebuild.h"
#include "util/fail.h"
#include "util/files.h"
#include "util/process.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <csignal>

class TestGolden : public QObject {
    Q_OBJECT
    QTemporaryDir t;
    QString repo, cache;
    QStringList modes; // what each remake started does, in turn
    int spawned = 0;
    QList<qint64> hangers;

    static QString git(const QString& dir, const QStringList& args) {
        proc::Options o;
        o.quiet = true;
        const proc::Result r = proc::capture(
            "git", QStringList{"-C", dir, "-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false"} + args, o);
        if (!r.ok()) qFatal("git %s: %s", qPrintable(args.join(' ')), r.err.constData());
        return r.text();
    }
    void put(const QString& rel, const QByteArray& data) {
        QDir().mkpath(QFileInfo(repo + "/" + rel).absolutePath());
        files::write(repo + "/" + rel, data);
    }
    // A merge onto main: the main checkout and origin/main move on together.
    QString merge(const QString& rel, const QByteArray& data) {
        put(rel, data);
        git(repo, {"add", "-A"});
        git(repo, {"commit", "-q", "-m", "merged: " + rel});
        git(repo, {"update-ref", "refs/remotes/origin/main", "HEAD"});
        return git(repo, {"rev-parse", "HEAD"});
    }
    qint64 run(const QString& mode) {
        QProcess p;
        p.setProgram("python3");
        p.setArguments({PI_QEMU_TEST_DATA "/fakebuild.py", cache, repo, mode});
        p.setStandardOutputFile(QProcess::nullDevice());
        qint64 pid = 0;
        if (!p.startDetached(&pid)) qFatal("cannot start python3");
        return pid;
    }
    golden::Options options() {
        golden::Options o;
        o.spawn = [this] { return run(modes.value(spawned++, "build")); };
        return o;
    }
    // The golden as a build of `commit` left it.
    void goldenAt(const QString& commit, bool clean = true) {
        source::Record r;
        r.commit = commit;
        r.clean = clean;
        r.made = "2026-10-17T10:00:00Z";
        QDir().mkpath(cache + "/golden");
        source::write(golden::recordFile(), r);
    }
    // A build of trimixxx0 that is still running, started `age` seconds ago
    // and allowed `most` minutes; until the test ends.
    void hanging(int age, int most) {
        const qint64 pid = run(QString("hang:%1:%2").arg(age).arg(most));
        hangers << pid;
        for (int i = 0; i < 40 && imagebuild::attempt("trimixxx0").pid != pid; i++) QTest::qWait(100);
        QVERIFY(imagebuild::running("trimixxx0"));
    }
    void endHangers() {
        for (const qint64 pid : hangers) ::kill(pid_t(pid), SIGKILL);
        for (int i = 0; i < 50 && imagebuild::running("trimixxx0"); i++) QTest::qWait(100);
        hangers.clear();
    }

private slots:
    void initTestCase() {
        QVERIFY(t.isValid());
        repo = t.filePath("main");
        cache = t.filePath("cache");
        qputenv("PI_QEMU_CACHE", QFile::encodeName(cache));
        qputenv("PI_QEMU_INSTANCES", QFile::encodeName(t.filePath("instances")));
        QDir().mkpath(repo);
        git(repo, {"init", "-q", "-b", "main"});
        put("pi-qemu/app/CMakeLists.txt", "# where paths finds the checkout\n");
        put("pi_config/a.service", "a\n");
        put("website/index.html", "w\n");
        git(repo, {"add", "-A"});
        git(repo, {"commit", "-q", "-m", "first"});
        git(repo, {"update-ref", "refs/remotes/origin/main", "HEAD"});
        QVERIFY(QDir::setCurrent(repo)); // before anything asks paths where the checkout is
    }
    void init() {
        modes.clear();
        spawned = 0;
        QFile::remove(imagebuild::attemptFile("trimixxx0"));
    }
    void cleanup() { endHangers(); }

    void mainsLatestIsLeftAlone() {
        goldenAt(git(repo, {"rev-parse", "HEAD"}));
        QCOMPARE(golden::ensure(options()).state, golden::State::Current);
        merge("website/index.html", "a website change\n"); // reaches no deck
        QCOMPARE(golden::ensure(options()).state, golden::State::Current);
        QCOMPARE(spawned, 0);
    }

    void aDeckChangeHasItMadeAgain() {
        const QString was = git(repo, {"rev-parse", "HEAD"});
        goldenAt(was);
        const QString now = merge("pi_config/a.service", "a deck change\n");
        golden::Options check;
        check.start = check.wait = false;
        QCOMPARE(golden::ensure(check).state, golden::State::Stale);
        const golden::Outcome out = golden::ensure(options());
        QCOMPARE(out.state, golden::State::Current);
        QCOMPARE(out.golden.commit, now);
        QCOMPARE(spawned, 1);
    }

    // Review round 1: two merges back to back. The second lands while the
    // first's remake builds; what was waited for is made for the main after.
    void aMainThatMovesOnMeanwhileIsTheOneMade() {
        goldenAt(git(repo, {"rev-parse", "HEAD"}));
        merge("pi_config/a.service", "merge X\n");
        modes = {"build-move", "build"};
        const golden::Outcome out = golden::ensure(options());
        QCOMPARE(out.state, golden::State::Current);
        QCOMPARE(spawned, 2);
        QCOMPARE(out.main, git(repo, {"rev-parse", "refs/remotes/origin/main"}));
        QCOMPARE(out.golden.commit, git(repo, {"rev-parse", "HEAD"}));
        QVERIFY(out.golden.clean);
    }

    void aFailedMainIsNotTriedAgainByItself() {
        goldenAt(git(repo, {"rev-parse", "HEAD"}));
        merge("pi_config/a.service", "a change that doesn't build\n");
        modes = {"fail"};
        QCOMPARE(golden::ensure(options()).state, golden::State::Failed);
        QCOMPARE(golden::ensure(options()).state, golden::State::Failed);
        QCOMPARE(spawned, 1); // not again
        golden::Options retry = options();
        retry.retry = true;
        QCOMPARE(golden::ensure(retry).state, golden::State::Current);
        QCOMPARE(spawned, 2);
    }

    void itWaitsForTheMainCheckout() {
        goldenAt(git(repo, {"rev-parse", "HEAD"}));
        merge("pi_config/a.service", "on main\n");
        put("pi_config/a.service", "being edited\n"); // the main checkout no longer holds main's code
        QCOMPARE(golden::ensure(options()).state, golden::State::Waiting);
        QCOMPARE(spawned, 0);
        git(repo, {"checkout", "-q", "--", "pi_config/a.service"});
    }

    // Review round 1: a remake that never ends holds nobody up.
    void aStuckRemakeIsNotWaitedFor() {
        goldenAt(git(repo, {"rev-parse", "HEAD"}));
        merge("pi_config/a.service", "stuck\n");
        hanging(180, 1); // three minutes in, one allowed
        QElapsedTimer t;
        t.start();
        QCOMPARE(golden::ensure(options()).state, golden::State::Stuck);
        QVERIFY(t.elapsed() < 10000);
        QCOMPARE(spawned, 0);
        QVERIFY(imagebuild::attempt("trimixxx0").stuck());
    }

    void aRemakeThatTurnsStuckWhileWaitedForIsLeft() {
        goldenAt(git(repo, {"rev-parse", "HEAD"}));
        merge("pi_config/a.service", "slow\n");
        hanging(55, 1); // a minute allowed, 55 s of it gone
        QElapsedTimer t;
        t.start();
        QCOMPARE(golden::ensure(options()).state, golden::State::Stuck);
        QVERIFY2(t.elapsed() < 20000, qPrintable(QString::number(t.elapsed())));
        QCOMPARE(spawned, 0);
    }

    void stopRefusesWhatIsNotStuckOrNotPiQemu() {
        hanging(10, 60);
        QVERIFY_THROWS_EXCEPTION(Failure, imagebuild::stop("trimixxx0", "test")); // ten seconds in
        endHangers();
        hanging(180, 1);
        try {
            imagebuild::stop("trimixxx0", "test"); // stuck, but python's: not ours to stop
            QFAIL("stopped a process that isn't pi-qemu");
        } catch (const Failure& f) {
            QVERIFY2(f.message.contains("is not pi-qemu: left alone"), qPrintable(f.message));
        }
        QVERIFY(imagebuild::running("trimixxx0"));
    }

    void aStuckRemakeIsStoppedAndCountsAsFailed() {
        goldenAt(git(repo, {"rev-parse", "HEAD"}));
        merge("pi_config/a.service", "stuck again\n");
        hanging(180, 1);
        const qint64 pid = imagebuild::attempt("trimixxx0").pid;
        imagebuild::stop("trimixxx0", "stopped: it looked stuck", proc::name(pid)); // the stand-in's own name
        QVERIFY(!imagebuild::running("trimixxx0"));
        const imagebuild::Attempt a = imagebuild::attempt("trimixxx0");
        QCOMPARE(a.state, QString("failed"));
        QCOMPARE(a.why, QString("stopped: it looked stuck"));
        QVERIFY(!a.ended.isEmpty());
        // That main counts as failed: not tried again by itself.
        QCOMPARE(golden::ensure(options()).state, golden::State::Failed);
        QCOMPARE(spawned, 0);
        hangers.clear();
    }
};

QTEST_GUILESS_MAIN(TestGolden)
#include "tst_golden.moc"
