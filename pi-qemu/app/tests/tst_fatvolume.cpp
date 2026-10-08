// board/fatvolume and board/nbdserver: a folder as a FAT32 stick, checked by
// tools that know nothing of them -- mtools reads the disk's tree and files,
// fsck_msdos checks its structure, and qemu-img reads it over NBD the way the
// emulated deck's QEMU does. Each is skipped where the tool is not installed.

#include "board/fatvolume.h"
#include "board/nbdserver.h"
#include "util/fail.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <fcntl.h>
#include <unistd.h>

namespace {

void writeFile(const QString& path, const QByteArray& content) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
}

QByteArray pattern(int n) {
    QByteArray b(n, 0);
    for (int i = 0; i < n; i++) b[i] = char((i * 7 + i / 4096) & 0xFF);
    return b;
}

// What a DJ's stick holds, at its most awkward. Names are written to the Mac
// decomposed where they have accents, as macOS hands such names back.
void makeStick(const QString& root) {
    writeFile(root + "/PIONEER/rekordbox/EXPORT.PDB", "pdb");
    writeFile(root + "/PIONEER/Artwork/00001/a1.jpg", "jpg");
    writeFile(root + "/Contents/Ignez & R\u00F8dha\u030Ad/Album/01 Track.aiff", pattern(70000)); // NFD
    writeFile(root + "/Contents/Cafe\u0301 del Mar.mp3", "mp3");                       // NFD
    writeFile(root + "/Contents/\U0001F3A7 set.mp3", "emoji");
    writeFile(root + "/Long File Name One.txt", "one");
    writeFile(root + "/Long File Name Two.txt", "two");
    writeFile(root + "/MiXeD.TXT", "mixed");
    writeFile(root + "/lower.txt", "lower");
    writeFile(root + "/.CacheData/x", "hidden folder");
    writeFile(root + "/empty.bin", "");
    writeFile(root + "/big.bin", pattern(300000));
    for (int i = 0; i < 600; i++) writeFile(QString("%1/many/file %2.txt").arg(root).arg(i, 3, 10, QChar('0')), QByteArray::number(i));
}

// The disk, as a sparse file: what qemu-img or a real stick would hold.
void dump(const FatVolume& v, const QString& path) {
    const int fd = ::open(QFile::encodeName(path).constData(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    QVERIFY(fd >= 0);
    QByteArray chunk(1 << 20, 0), zero(1 << 20, 0);
    for (quint64 at = 0; at < v.size(); at += quint64(chunk.size())) {
        const quint64 n = std::min<quint64>(quint64(chunk.size()), v.size() - at);
        QString error;
        QVERIFY2(v.read(at, chunk.data(), n, &error), qPrintable(error));
        if (std::memcmp(chunk.constData(), zero.constData(), n) != 0)
            QCOMPARE(::pwrite(fd, chunk.constData(), n, off_t(at)), ssize_t(n));
    }
    QCOMPARE(::ftruncate(fd, off_t(v.size())), 0);
    ::close(fd);
}

QString tool(const QString& name) {
    for (const QString& dir : {"/opt/homebrew/bin", "/usr/local/bin", "/sbin", "/usr/sbin"})
        if (QFileInfo(dir + "/" + name).isExecutable()) return dir + "/" + name;
    return {};
}

QByteArray runTool(const QString& program, const QStringList& args, int* status = nullptr,
                   const QString& name = {}) {
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // QProcess hands its arguments over decomposed on macOS (QFile::encodeName),
    // so a name that must arrive as written goes in the environment instead.
    if (!name.isEmpty()) env.insert("NAME", name);
    env.insert("LC_ALL", "en_US.UTF-8");
    env.insert("MTOOLSRC", "/dev/null");
    env.insert("MTOOLS_SKIP_CHECK", "1");
    if (args.size() == 4 && program == "/bin/sh") env.insert("IMG", args[3]);
    p.setProcessEnvironment(env);
    p.start(program, args);
    p.waitForFinished(60000);
    if (status) *status = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    return p.readAllStandardOutput() + p.readAllStandardError();
}

} // namespace

class TestFatVolume : public QObject {
    Q_OBJECT

private slots:
    // The tree mtools reads off the disk: every name the stick should have,
    // long names in NFC, and the files' bytes.
    void treeAndFiles() {
        const QString mdir = tool("mdir"), mtype = tool("mtype");
        if (mdir.isEmpty() || mtype.isEmpty()) QSKIP("mtools is not installed (brew install mtools)");
        QTemporaryDir tmp;
        makeStick(tmp.filePath("stick"));
        FatSpec spec;
        spec.folder = tmp.filePath("stick");
        spec.size = 300ull << 20;
        spec.label = "TESTSTICK";
        const FatVolume v(spec);
        dump(v, tmp.filePath("disk.img"));
        const QString img = tmp.filePath("disk.img") + "@@" + QString::number(2048 * 512);

        const QString tree = QString::fromUtf8(runTool(mdir, {"-/", "-b", "-a", "-i", img, "::/"}));
        for (const QString& want : {"::/PIONEER/rekordbox/EXPORT.PDB", "::/PIONEER/Artwork/00001/a1.jpg",
                                    "::/Contents/Ignez & R\u00F8dh\u00E5d/Album/01 Track.aiff", "::/Contents/Caf\u00E9 del Mar.mp3",
                                    "::/Long File Name One.txt",
                                    "::/Long File Name Two.txt", "::/MiXeD.TXT", "::/lower.txt", "::/.CacheData/x",
                                    "::/empty.bin", "::/big.bin", "::/many/file 000.txt", "::/many/file 599.txt"})
            QVERIFY2(tree.contains(want + "\n") || tree.endsWith(want), qPrintable("missing " + want + " in\n" + tree.left(2000)));
        QVERIFY2(tree.normalized(QString::NormalizationForm_C) == tree, "a name came out decomposed");
        // mtools prints a name past the BMP (the emoji's surrogate pair) as an
        // empty one, so that file is left to the deck, whose Linux reads it.

        // 8.3 names: as they are, lower case by its flags, or made up with tails.
        const QString full = QString::fromUtf8(runTool(mdir, {"-a", "-i", img, "::/"}));
        QVERIFY2(full.contains("LONGFI~1 TXT") && full.contains("LONGFI~2 TXT"), qPrintable(full));
        QVERIFY2(full.contains("lower    txt") || full.contains("lower.txt"), qPrintable(full));
        QVERIFY2(full.contains("TESTSTICK"), qPrintable(full));

        QCOMPARE(runTool(mtype, {"-i", img, "::/big.bin"}), pattern(300000));
        QCOMPARE(runTool("/bin/sh", {"-c", mtype + " -i \"$IMG\" \"$NAME\"", "-", img}, nullptr,
                         "::/Contents/Ignez & R\u00F8dh\u00E5d/Album/01 Track.aiff"),
                 pattern(70000));
        QCOMPARE(runTool(mtype, {"-i", img, "::/many/file 417.txt"}), QByteArray("417"));
        QCOMPARE(runTool(mtype, {"-i", img, "::/empty.bin"}), QByteArray());
    }

    // No partition table, and a structure fsck_msdos finds nothing wrong with.
    void superfloppyChecksClean() {
        const QString fsck = tool("fsck_msdos");
        if (fsck.isEmpty()) QSKIP("no fsck_msdos");
        QTemporaryDir tmp;
        makeStick(tmp.filePath("stick"));
        FatSpec spec;
        spec.folder = tmp.filePath("stick");
        spec.superfloppy = true;
        spec.size = 2200ull << 20;
        const FatVolume v(spec);
        dump(v, tmp.filePath("disk.img"));
        int status = 0;
        const QByteArray out = runTool(fsck, {"-n", tmp.filePath("disk.img")}, &status);
        QVERIFY2(status == 0, out.constData());
        QFile img(tmp.filePath("disk.img"));
        QVERIFY(img.open(QIODevice::ReadOnly));
        const QByteArray boot = img.read(512);
        QCOMPARE(boot.mid(82, 8), QByteArray("FAT32   ")); // the filesystem from sector 0
        QCOMPARE(boot.mid(510, 2), QByteArray("\x55\xAA", 2));
    }

    // A real stick's own first sectors are served exactly as they were --
    // dirty flag, serial, boot code -- and the rest is laid out to match them.
    void realVolumeStart() {
        QTemporaryDir tmp;
        makeStick(tmp.filePath("stick"));
        FatSpec first;
        first.folder = tmp.filePath("stick");
        first.superfloppy = true;
        first.size = 2200ull << 20;
        first.serial = 0xE02C5D1B;
        const FatVolume a(first);
        QByteArray reserved(32 * 512, 0);
        QString error;
        QVERIFY(a.read(0, reserved.data(), quint64(reserved.size()), &error));
        reserved[0x41] = 0x01;                   // left dirty by its last computer
        reserved[200] = 'B';                     // somebody's boot code
        FatSpec second = first;
        second.serial = 0;
        second.volumeStart = reserved;
        const FatVolume b(second);
        QByteArray sector(512, 0);
        QVERIFY(b.read(0, sector.data(), 512, &error));
        QCOMPARE(sector, reserved.left(512));
        QCOMPARE(sector.mid(67, 4), QByteArray("\x1B\x5D\x2C\xE0", 4));
        // The same geometry, so the same FATs and data.
        QByteArray x(1 << 20, 0), y(1 << 20, 0);
        QVERIFY(a.read(32 * 512, x.data(), quint64(x.size()), &error));
        QVERIFY(b.read(32 * 512, y.data(), quint64(y.size()), &error));
        QCOMPARE(x, y);
    }

    // Over NBD, read by QEMU's own client: the same bytes as read directly.
    void servedOverNbd() {
        const QString qemuImg = tool("qemu-img");
        if (qemuImg.isEmpty()) QSKIP("qemu-img is not installed (brew install qemu)");
        QTemporaryDir tmp;
        makeStick(tmp.filePath("stick"));
        FatSpec spec;
        spec.folder = tmp.filePath("stick");
        spec.size = 300ull << 20;
        auto v = std::make_shared<FatVolume>(spec);
        dump(*v, tmp.filePath("direct.img"));

        // A short path: a socket's must fit in 104 bytes.
        const QString socket = QString("/tmp/tst-fat-%1.nbd").arg(QCoreApplication::applicationPid());
        NbdServer server(v, socket);
        QString error;
        QVERIFY2(server.start(&error), qPrintable(error));
        int status = 0;
        const QByteArray out = runTool(qemuImg, {"convert", "-f", "raw", "-O", "raw",
                                                 "nbd+unix:///stick?socket=" + socket, tmp.filePath("served.img")}, &status);
        QVERIFY2(status == 0, out.constData());
        auto sha = [](const QString& path) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) return QByteArray();
            QCryptographicHash h(QCryptographicHash::Sha256);
            h.addData(&f);
            return h.result();
        };
        QCOMPARE(QFileInfo(tmp.filePath("served.img")).size(), qint64(v->size()));
        QCOMPARE(sha(tmp.filePath("served.img")), sha(tmp.filePath("direct.img")));
        server.stop();
        QVERIFY(!QFileInfo::exists(socket));
    }

    // What a read lands on, by name: what the read report counts.
    void describesWhatIsWhere() {
        QTemporaryDir tmp;
        makeStick(tmp.filePath("stick"));
        FatSpec spec;
        spec.folder = tmp.filePath("stick");
        spec.size = 300ull << 20;
        const FatVolume v(spec);
        QCOMPARE(v.describe(0), QString("partition table"));
        QCOMPARE(v.describe(4096), QString("before the partition"));
        QCOMPARE(v.describe(2048 * 512), QString("boot sectors"));
        QCOMPARE(v.describe(2048 * 512 + 32 * 512), QString("FAT"));
        QCOMPARE(v.describe(v.size() - 1), QString("free space"));
        // Everything up to the free space: the folders and files, each found.
        QHash<QString, int> seen;
        for (quint64 at = 2048 * 512; at < v.size(); at += 4096) {
            const QString what = v.describe(at);
            if (what == "free space") break;
            seen[what]++;
        }
        QVERIFY2(seen.contains("folder /"), qPrintable(QStringList(seen.keys()).join(", ")));
        QVERIFY2(seen.contains("folder /PIONEER/"), qPrintable(QStringList(seen.keys()).join(", ")));
        QVERIFY2(seen.contains("/PIONEER/rekordbox/EXPORT.PDB"), qPrintable(QStringList(seen.keys()).join(", ")));
    }

    // What cannot be a FAT32 stick is said, not served wrong.
    void refusals() {
        QTemporaryDir tmp;
        QDir().mkpath(tmp.filePath("stick"));
        QFile big(tmp.filePath("stick/a.bin")); // 200 MiB that take no room: a hole
        QVERIFY(big.open(QIODevice::WriteOnly) && big.resize(200ll << 20));
        big.close();
        FatSpec spec;
        spec.folder = tmp.filePath("nowhere");
        QVERIFY_THROWS_EXCEPTION(Failure, FatVolume{spec});
        spec.folder = tmp.filePath("stick");
        spec.size = 10ull << 20;
        QVERIFY_THROWS_EXCEPTION(Failure, FatVolume{spec}); // too small for FAT32 at all
        spec.size = 100ull << 20;
        QVERIFY_THROWS_EXCEPTION(Failure, FatVolume{spec}); // the file does not fit
        spec.size = 0;                                      // sized to fit
        const FatVolume v(spec);
        QVERIFY(v.size() >= (200ull << 20));
    }
};

QTEST_MAIN(TestFatVolume)
#include "tst_fatvolume.moc"
