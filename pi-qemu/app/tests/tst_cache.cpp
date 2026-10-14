// cache/: the move into .cache/ and archives/ on a scratch root, twice, and
// back; prune clearing the cache, never the archives, never what is in use.

#include "board/sticks.h"
#include "cache/cache.h"
#include "cache/dockeruse.h"
#include "cache/places.h"
#include "util/fail.h"
#include "util/files.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>

class TestCache : public QObject {
    Q_OBJECT
    QTemporaryDir t; // the scratch root: everything the move touches is under it
    QString root, cdjs;

    QString at(const QString& rel) const { return t.filePath(rel); }
    void make(const QString& rel, const QByteArray& data = "x") {
        QDir().mkpath(QFileInfo(at(rel)).absolutePath());
        files::write(at(rel), data);
    }
    static ino_t inode(const QString& path) {
        struct stat st;
        return ::lstat(QFile::encodeName(path).constData(), &st) == 0 ? st.st_ino : 0;
    }
    // Every file under `dir`, relative, sorted: a tree's shape.
    static QStringList tree(const QString& dir) {
        QStringList out;
        QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (it.hasNext()) out << QDir(dir).relativeFilePath(it.next());
        out.sort();
        return out;
    }
    static bool fails(const std::function<void()>& f) {
        try { f(); } catch (const Failure&) { return true; }
        return false;
    }

private slots:
    // The layout before .cache/ and archives/, as pi-qemu left it.
    void initTestCase() {
        QVERIFY(t.isValid());
        root = t.path();
        cdjs = at("home/.pi-qemu/cdj");
        qputenv("PI_QEMU_CDJ", QFile::encodeName(cdjs));
        qputenv("PI_QEMU_INSTANCES", QFile::encodeName(at("home/.pi-qemu/instances")));
        qputenv("PI_QEMU_LINKS", QFile::encodeName(at("home/.pi-qemu/links")));
        qputenv("PI_QEMU_CACHE", QFile::encodeName(root + "/.cache"));
        qputenv("PI_QEMU_ARCHIVES", QFile::encodeName(root + "/archives"));
        QDir().mkpath(at("home/.pi-qemu/instances/sam"));
        make("pi-qemu/.cache/2026-09-15-raspios-trixie-arm64-lite.img");
        make("pi-qemu/.cache/2026-09-15-raspios-trixie-arm64-lite.img.xz");
        make("pi-qemu/.cache/.DS_Store");
        make("pi-qemu/.cache/base/trimixxx0-858194c619350e67.img");
        make("pi-qemu/.cache/build/trimixxx0.img");
        make("pi-qemu/.cache/build/trimixxx0.log");
        make("pi-qemu/.cache/golden/trimixxx0.img", "golden");
        make("pi-qemu/.cache/golden/trimixxx0.state");
        make("pi-qemu/.cache/decks/trimixxx1/ssh/ssh_host_ed25519_key");
        make("pi-qemu/.cache/captures/trimixxx1-20261009-1639/files.tgz");
        make("pi-qemu/.cache/sticks/SAM1.stick");
        make("pi-qemu/.cache/sticks/earlier-2205/PIONEER/x");
        make("pi-qemu/qemu/.build/qemu-11.1.0/build/qemu-system-aarch64");
        QDir().mkpath(at("pi-qemu/qemu/.build/bin"));
        QVERIFY(QFile::link("../qemu-11.1.0/build/qemu-system-aarch64", at("pi-qemu/qemu/.build/bin/qemu-system-aarch64")));
        make("pi-qemu/release/out/0.1.1/trimixxx-0.1.1.raucb");
        make("home/.pi-qemu/cdj/firmware/nxs/main-firmware.bin");
        make("home/.pi-qemu/cdj/firmware.new/nxs/half");       // an extraction cut short: stays
        make("home/.pi-qemu/cdj/qemu-55347990/VERSION");
        make("home/.pi-qemu/cdj/sam1/PIONEER/rekordbox/export.pdb");
        make("home/.pi-qemu/cdj/sam1-usb.img");
        make("home/.pi-qemu/cdj/a/cdj.json");                   // a CDJ: stays
        make("home/.pi-qemu/cdj/.DS_Store");
    }

    void theOldLayoutWaits() {
        const QStringList w = cache::places::waiting(root, cdjs);
        QVERIFY(w.contains(root + "/pi-qemu/.cache/golden"));
        QVERIFY(w.contains(cdjs + "/firmware"));
        QVERIFY(!w.contains(cdjs + "/sam1")); // people's own: moved, but nothing waits for it
    }

    void nothingMovesWhileSomethingRuns() {
        QVERIFY(fails([&] { cache::migrate(root, false, {{"deck sam (pid 1)", {"qemu"}}}); }));
        QVERIFY(QFileInfo::exists(root + "/pi-qemu/.cache/golden/trimixxx0.img"));
        QVERIFY(!QFileInfo::exists(root + "/.cache"));
    }

    void theMove() {
        const ino_t golden = inode(root + "/pi-qemu/.cache/golden/trimixxx0.img");
        cache::migrate(root, false, {});
        // Renamed, not copied.
        QCOMPARE(inode(root + "/.cache/golden/trimixxx0.img"), golden);
        for (const char* f : {".cache/stock/2026-09-15-raspios-trixie-arm64-lite.img",
                              ".cache/stock/2026-09-15-raspios-trixie-arm64-lite.img.xz",
                              ".cache/base/trimixxx0-858194c619350e67.img", ".cache/build/trimixxx0.log",
                              ".cache/golden/trimixxx0.state", ".cache/cdj-qemu/55347990/VERSION",
                              "archives/decks/trimixxx1/ssh/ssh_host_ed25519_key",
                              "archives/captures/trimixxx1-20261009-1639/files.tgz", "archives/sticks/SAM1.stick",
                              "archives/sticks/earlier-2205/PIONEER/x",
                              "archives/sticks/cdj/sam1/PIONEER/rekordbox/export.pdb", "archives/sticks/cdj/sam1-usb.img",
                              "archives/releases/0.1.1/trimixxx-0.1.1.raucb", "archives/cdj-firmware/nxs/main-firmware.bin",
                              ".cache/.gitignore", "archives/.gitignore"})
            QVERIFY2(QFileInfo::exists(root + "/" + f), f);
        // QEMU's link is relative: it follows its build.
        QCOMPARE(QFileInfo(root + "/.cache/qemu/bin/qemu-system-aarch64").canonicalFilePath(),
                 QFileInfo(root + "/.cache/qemu/qemu-11.1.0/build/qemu-system-aarch64").canonicalFilePath());
        // The old places: gone, but for a CDJ's own state and what wasn't pi-qemu's.
        for (const char* gone : {"pi-qemu/.cache", "pi-qemu/qemu/.build", "pi-qemu/release/out"})
            QVERIFY2(!QFileInfo::exists(root + "/" + gone), gone);
        QVERIFY(!QFileInfo::exists(cdjs + "/firmware"));
        QVERIFY(QFileInfo::exists(cdjs + "/a/cdj.json"));
        QVERIFY(QFileInfo::exists(cdjs + "/firmware.new/nxs/half"));
        // The decks' and CDJs' own state, linked.
        QCOMPARE(QFileInfo(root + "/.cache/instances").symLinkTarget(), at("home/.pi-qemu/instances"));
        QCOMPARE(QFileInfo(root + "/.cache/cdj").symLinkTarget(), cdjs);
        QVERIFY(cache::places::waiting(root, cdjs).isEmpty());
    }

    // "my usb", `stick insert SAM1`, is still his own stick (SAM1.stick),
    // with his rekordbox copy, sam1/, moved in beside the sticks.
    void sam1IsStillHisStick() {
        QCOMPARE(Sticks::find(root + "/archives/sticks", "SAM1"), root + "/archives/sticks/SAM1.stick");
        // And side by side, on this disk that ignores case: the name as written first.
        make("side/sam1/PIONEER/x");
        make("side/SAM1.stick");
        make("side/earlier-2205/x");
        make("side/EARLIER-2205.stick");
        QCOMPARE(Sticks::find(at("side"), "SAM1"), at("side/SAM1.stick"));
        QCOMPARE(Sticks::find(at("side"), "sam1"), at("side/sam1"));
        QCOMPARE(Sticks::find(at("side"), "EARLIER-2205"), at("side/EARLIER-2205.stick"));
        QVERIFY(Sticks::find(at("side"), "nothing").isEmpty());
    }

    // Docker makes pi-qemu/release/out again, empty, for each release
    // container: nothing to move, nothing waiting.
    void anEmptyOldFolderIsNothing() {
        QVERIFY(QDir().mkpath(root + "/pi-qemu/release/out"));
        QVERIFY(cache::places::planned(root, cdjs, root).isEmpty());
        QVERIFY(QDir().rmdir(root + "/pi-qemu/release/out"));
    }

    void twiceIsOnce() {
        const QStringList before = tree(root);
        cache::migrate(root, false, {});
        QCOMPARE(tree(root), before);
    }

    void andBack() {
        cache::migrate(root, true, {});
        QVERIFY(QFileInfo::exists(root + "/pi-qemu/.cache/golden/trimixxx0.img"));
        QVERIFY(QFileInfo::exists(root + "/pi-qemu/.cache/2026-09-15-raspios-trixie-arm64-lite.img.xz"));
        QVERIFY(QFileInfo::exists(root + "/pi-qemu/qemu/.build/bin/qemu-system-aarch64"));
        QVERIFY(QFileInfo::exists(root + "/pi-qemu/release/out/0.1.1/trimixxx-0.1.1.raucb"));
        QVERIFY(QFileInfo::exists(cdjs + "/firmware/nxs/main-firmware.bin"));
        QVERIFY(QFileInfo::exists(cdjs + "/sam1/PIONEER/rekordbox/export.pdb"));
        QVERIFY(QFileInfo::exists(cdjs + "/qemu-55347990/VERSION"));
        QVERIFY(!QFileInfo::exists(root + "/.cache"));
        QVERIFY(!QFileInfo::exists(root + "/archives"));
        cache::migrate(root, false, {}); // and there again
        QVERIFY(QFileInfo::exists(root + "/.cache/golden/trimixxx0.img"));
        QVERIFY(QFileInfo::exists(root + "/archives/sticks/cdj/sam1-usb.img"));
    }

    // An older pi-qemu, run after the move, makes its old folders again:
    // what is new moves in, what both have stays where it was.
    void whatAnOlderPiQemuLeft() {
        make("pi-qemu/.cache/build/trimixxx0.img", "older");
        make("pi-qemu/.cache/build/onetest.img");
        QVERIFY(cache::places::waiting(root, cdjs).isEmpty()); // .cache/build is there: nothing stops
        cache::migrate(root, false, {});
        QVERIFY(QFileInfo::exists(root + "/.cache/build/onetest.img"));
        QCOMPARE(files::read(root + "/pi-qemu/.cache/build/trimixxx0.img"), QByteArray("older"));
        QCOMPARE(files::read(root + "/.cache/build/trimixxx0.img"), QByteArray("x"));
        QFile::remove(root + "/pi-qemu/.cache/build/trimixxx0.img");
    }

    // What can't go back yet (its old place taken again) stays recorded, and
    // goes back on another --undo once its place is free.
    void undoKeepsWhatCantGoBack() {
        QVERIFY(QFileInfo::exists(root + "/pi-qemu/.cache/build")); // the older pi-qemu's folder, now empty
        cache::migrate(root, true, {});
        QVERIFY(QFileInfo::exists(root + "/pi-qemu/.cache/build/onetest.img"));
        QVERIFY(QFileInfo::exists(root + "/.cache/build/trimixxx0.img")); // its place was taken
        QVERIFY(QFileInfo::exists(root + "/pi-qemu/.cache/golden/trimixxx0.img"));
        QVERIFY(QFile::rename(root + "/pi-qemu/.cache/build/onetest.img", at("onetest.img")));
        QVERIFY(QDir().rmdir(root + "/pi-qemu/.cache/build"));
        cache::migrate(root, true, {});
        QVERIFY(QFileInfo::exists(root + "/pi-qemu/.cache/build/trimixxx0.img"));
        QVERIFY(!QFileInfo::exists(root + "/.cache"));
        cache::migrate(root, false, {});
        QVERIFY(cache::places::waiting(root, cdjs).isEmpty());
    }

    // A root that isn't the main checkout, a scratch copy, takes nothing from
    // outside it: not ~/.pi-qemu/cdj (the NXS firmware, Sam's copy), whatever
    // PI_QEMU_CDJ says. Deleting the copy then deletes nothing of his.
    void aScratchRootTakesNothingFromHome() {
        QTemporaryDir home, scratch;
        const QByteArray wasHome = qgetenv("HOME"), wasCdj = qgetenv("PI_QEMU_CDJ");
        auto restore = qScopeGuard([&] {
            qputenv("HOME", wasHome);
            qputenv("PI_QEMU_CDJ", wasCdj);
        });
        for (const char* f : {".pi-qemu/cdj/firmware/nxs/main-firmware.bin", ".pi-qemu/cdj/sam1/PIONEER/x",
                              ".pi-qemu/cdj/sam1-usb.img", ".pi-qemu/cdj/qemu-55347990/VERSION"}) {
            QDir().mkpath(QFileInfo(home.filePath(f)).absolutePath());
            files::write(home.filePath(f), "x");
        }
        QDir().mkpath(scratch.filePath("pi-qemu/.cache/golden"));
        files::write(scratch.filePath("pi-qemu/.cache/golden/trimixxx0.img"), "x");
        const QStringList before = tree(home.path());
        qputenv("HOME", QFile::encodeName(home.path()));
        qunsetenv("PI_QEMU_CDJ");
        cache::migrate(scratch.path(), false, {});
        QCOMPARE(tree(home.path()), before);
        QVERIFY(QFileInfo::exists(scratch.filePath(".cache/golden/trimixxx0.img")));
        QVERIFY(!QFileInfo::exists(scratch.filePath("archives/cdj-firmware")));
        // Named by PI_QEMU_CDJ, the real one stays as it is too.
        qputenv("PI_QEMU_CDJ", QFile::encodeName(home.filePath(".pi-qemu/cdj")));
        cache::migrate(scratch.path(), false, {});
        cache::migrate(scratch.path(), true, {});
        QCOMPARE(tree(home.path()), before);
        QVERIFY(QFileInfo::exists(scratch.filePath("pi-qemu/.cache/golden/trimixxx0.img")));
    }

    void pruneNeverArchives() {
        for (const QString& k : {"releases", "sticks", "decks", "cdj-firmware", "captures", "instances", "nonsense"})
            QVERIFY2(fails([&] { cache::prune({k}, {}); }), qPrintable(k));
        QVERIFY(QFileInfo::exists(root + "/archives/releases/0.1.1/trimixxx-0.1.1.raucb"));
    }

    void pruneLeavesWhatIsInUse() {
        QVERIFY(fails([&] { cache::prune({"base", "qemu"}, {{"deck sam (pid 1)", {"qemu"}}}); }));
        QVERIFY(QFileInfo::exists(root + "/.cache/base/trimixxx0-858194c619350e67.img")); // a refusal changes nothing
        cache::prune({"base"}, {{"deck sam (pid 1)", {"qemu"}}});
        QVERIFY(!QFileInfo::exists(root + "/.cache/base"));
        QVERIFY(QFileInfo::exists(root + "/.cache/qemu/bin"));
    }

    void pruneEverything() {
        const QStringList archives = tree(root + "/archives");
        cache::prune({"stock", "base", "build", "golden", "qemu", "cdj-qemu"}, {});
        for (const char* k : {"stock", "base", "build", "golden", "qemu", "cdj-qemu"})
            QVERIFY2(!QFileInfo::exists(root + "/.cache/" + k), k);
        QCOMPARE(tree(root + "/archives"), archives);
        // The decks' and CDJs' state: their links stay, and what they lead to.
        QVERIFY(QFileInfo(root + "/.cache/instances").isSymLink());
        QVERIFY(QFileInfo::exists(at("home/.pi-qemu/instances/sam")));
        QVERIFY(QFileInfo::exists(cdjs + "/a/cdj.json"));
    }

    // A kind's folder that is a link: the link goes, not what it leads to.
    void pruneNeverFollowsALink() {
        make("elsewhere/precious");
        QVERIFY(QFile::link(at("elsewhere"), root + "/.cache/build"));
        cache::prune({"build"}, {});
        QVERIFY(!cache::places::there(root + "/.cache/build"));
        QVERIFY(QFileInfo::exists(at("elsewhere/precious")));
    }

    void dockerSizes() {
        QCOMPARE(cache::dockerBytes("556.6MB"), 556'600'000);
        QCOMPARE(cache::dockerBytes("1.506GB"), 1'506'000'000);
        QCOMPARE(cache::dockerBytes("4.096kB"), 4'096);
        QCOMPARE(cache::dockerBytes("0B"), 0);
        QCOMPARE(cache::dockerBytes("n/a"), 0);
    }
};

QTEST_GUILESS_MAIN(TestCache)
#include "tst_cache.moc"
