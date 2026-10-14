// util/diskuse: a tree's sizes as APFS has them, clones' shared blocks apart.

#include "util/diskuse.h"
#include "util/files.h"

#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

class TestDiskuse : public QObject {
    Q_OBJECT
    QTemporaryDir dir; // TMPDIR: on the Mac's APFS volume

    static QByteArray noise(int bytes) {
        QByteArray b(bytes, Qt::Uninitialized);
        QRandomGenerator g(7);
        for (int i = 0; i < bytes; i += 4) {
            const quint32 v = g.generate();
            std::memcpy(b.data() + i, &v, std::min(4, bytes - i));
        }
        return b;
    }

private slots:
    void aFileIsItsOwn() {
        files::write(dir.filePath("a.img"), noise(1 << 20));
        const diskuse::Use u = diskuse::of(dir.filePath("a.img"));
        QCOMPARE(u.files, 1);
        QCOMPARE(u.apparent, 1 << 20);
        QVERIFY(u.allocated >= 1 << 20);
        QCOMPARE(u.own, u.allocated);
    }
    // A clone shares every block with its original: neither owns them now.
    // Writing into the clone gives it blocks of its own, only those.
    void aCloneSharesItsBlocks() {
        files::write(dir.filePath("b.img"), noise(1 << 20));
        files::clone(dir.filePath("b.img"), dir.filePath("b-clone.img"));
        QCOMPARE(diskuse::of(dir.filePath("b.img")).own, 0);
        QCOMPARE(diskuse::of(dir.filePath("b-clone.img")).own, 0);
        QFile f(dir.filePath("b-clone.img"));
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.seek(1 << 19));
        QCOMPARE(f.write(noise(64 << 10)), 64 << 10);
        f.close();
        const diskuse::Use c = diskuse::of(dir.filePath("b-clone.img"));
        QVERIFY(c.own >= 64 << 10 && c.own < c.allocated);
    }
    void aSparseFileTakesItsData() {
        QFile f(dir.filePath("sparse.img"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(noise(4096)), 4096);
        QVERIFY(f.resize(100 << 20));
        f.close();
        const diskuse::Use u = diskuse::of(dir.filePath("sparse.img"));
        QCOMPARE(u.apparent, 100 << 20);
        QVERIFY(u.allocated < 1 << 20);
    }
    // A tree: its files and its directories' files, its symlinks not followed.
    void aTree() {
        QDir().mkpath(dir.filePath("tree/sub/deeper"));
        for (int i = 0; i < 300; i++) // more than one getattrlistbulk batch's worth of names
            files::write(dir.filePath(QString("tree/sub/f%1").arg(i)), noise(100));
        files::write(dir.filePath("tree/sub/deeper/big"), noise(1 << 20));
        QVERIFY(QFile::link(dir.filePath("a.img"), dir.filePath("tree/link-to-a")));
        QVERIFY(QFile::link(dir.filePath("tree/sub"), dir.filePath("tree/link-to-sub")));
        const diskuse::Use u = diskuse::of(dir.filePath("tree"));
        QCOMPARE(u.files, 301);
        QCOMPARE(u.apparent, 300 * 100 + (1 << 20));
        QCOMPARE(diskuse::of(dir.filePath("tree/link-to-a")).files, 0);
    }
    void nothingThere() {
        const diskuse::Use u = diskuse::of(dir.filePath("none"));
        QCOMPARE(u.files, 0);
        QCOMPARE(u.allocated, 0);
    }
    void text() {
        QCOMPARE(diskuse::text(0), QString("0 B"));
        QCOMPARE(diskuse::text(1500), QString("2 KB"));
        QCOMPARE(diskuse::text(120'400'000), QString("120 MB"));
        QCOMPARE(diskuse::text(1'440'000'000), QString("1.4 GB"));
        QVERIFY(diskuse::freeBytes(dir.path()) > 0);
    }
};

QTEST_GUILESS_MAIN(TestDiskuse)
#include "tst_diskuse.moc"
