#include "util/files.h"

#include "util/fail.h"
#include "util/process.h"

#include <QFile>
#include <QScopeGuard>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __APPLE__
#include <sys/clonefile.h>
#endif

namespace files {

namespace {

// A hole is whole 64 KiB blocks of zeros: 96% of what 4 KiB ones would free
// on a deck's card, all of it on a release card, in far fewer extents.
constexpr qint64 kHole = 64 * 1024;
constexpr qint64 kChunk = 8 * 1024 * 1024; // a read; a multiple of kHole

bool readAt(int fd, char* buf, qint64 n, qint64 at) {
    while (n > 0) {
        const ssize_t r = ::pread(fd, buf, size_t(n), at);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        buf += r;
        n -= r;
        at += r;
    }
    return true;
}

bool zeros(const char* p, qint64 n) { return n == 0 || (p[0] == 0 && std::memcmp(p, p + 1, size_t(n - 1)) == 0); }

// Each stretch of fd that holds data, read in chunks that never straddle a
// multiple of kChunk: f(offset, bytes, length). Holes are skipped (SEEK_DATA).
bool eachChunk(int fd, qint64 size, char* buf, const std::function<bool(qint64, const char*, qint64)>& f) {
    for (qint64 off = 0; off < size;) {
        const off_t start = ::lseek(fd, off, SEEK_DATA);
        if (start < 0) return errno == ENXIO; // no data after off
        const off_t end = ::lseek(fd, start, SEEK_HOLE);
        if (end < 0) return false;
        for (qint64 at = start; at < end;) {
            const qint64 n = std::min<qint64>(end, (at / kChunk + 1) * kChunk) - at;
            if (!readAt(fd, buf, n, at) || !f(at, buf, n)) return false;
            at += n;
        }
        off = end;
    }
    return true;
}

} // namespace

QByteArray read(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) fail("cannot read " + path + ": " + f.errorString());
    return f.readAll();
}

void write(const QString& path, const QByteArray& data, bool privateFile) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) fail("cannot write " + path + ": " + f.errorString());
    if (privateFile) f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (f.write(data) != data.size()) fail("cannot write " + path + ": " + f.errorString());
}

void clone(const QString& from, const QString& to) {
    QFile::remove(to);
#ifdef __APPLE__
    if (::clonefile(QFile::encodeName(from).constData(), QFile::encodeName(to).constData(), 0) == 0) return;
#endif
    if (!QFile::copy(from, to)) fail("cannot copy " + from + " to " + to);
}

bool inUse(const QString& path) {
    proc::Options o;
    o.quiet = true;
    return proc::capture("lsof", {path}, o).ok();
}

Sparse sparsify(const QString& path) {
    Sparse s;
    auto no = [&s](const QString& why) {
        s.error = why;
        return s;
    };
#ifdef __APPLE__
    const QByteArray file = QFile::encodeName(path), copy = QFile::encodeName(path + ".sparse");
    struct stat st {};
    if (::stat(file.constData(), &st) != 0) return no(QString("cannot read %1: %2").arg(path, strerror(errno)));
    s.before = s.after = qint64(st.st_blocks) * 512;
    ::unlink(copy.constData()); // left by a run that was killed
    if (::clonefile(file.constData(), copy.constData(), 0) != 0)
        return no(QString("cannot clone %1: %2").arg(path, strerror(errno)));
    auto drop = qScopeGuard([&] { ::unlink(copy.constData()); });
    const int in = ::open(file.constData(), O_RDONLY), out = ::open(copy.constData(), O_RDWR);
    auto close = qScopeGuard([&] {
        if (in >= 0) ::close(in);
        if (out >= 0) ::close(out);
    });
    if (in < 0 || out < 0) return no(QString("cannot open %1: %2").arg(path, strerror(errno)));
    // Past the page cache: these files are bigger than the Mac's free memory.
    ::fcntl(in, F_NOCACHE, 1);
    ::fcntl(out, F_NOCACHE, 1);
    QByteArray a(kChunk, Qt::Uninitialized), b(kChunk, Qt::Uninitialized);

    // The copy's zeros: each run of whole kHole blocks, one hole.
    qint64 run = -1, runEnd = -1;
    auto punch = [&] {
        if (run < 0) return true;
        fpunchhole_t h{};
        h.fp_offset = run;
        h.fp_length = runEnd - run;
        run = -1;
        return ::fcntl(out, F_PUNCHHOLE, &h) == 0;
    };
    const bool punched = eachChunk(out, st.st_size, a.data(), [&](qint64 at, const char* p, qint64 n) {
        for (qint64 h = (at + kHole - 1) / kHole * kHole; h + kHole <= at + n; h += kHole) {
            if (!zeros(p + (h - at), kHole)) {
                if (!punch()) return false;
            } else if (run >= 0 && runEnd == h) {
                runEnd += kHole;
            } else {
                if (!punch()) return false;
                run = h;
                runEnd = h + kHole;
            }
        }
        return true;
    }) && punch();
    if (!punched) return no(QString("cannot make holes in a copy of %1: %2").arg(path, strerror(errno)));

    // Read back equal: wherever the file holds data, the copy holds the same
    // bytes; where the file has holes, so has the copy (a clone, only punched).
    bool same = true;
    const bool read = eachChunk(in, st.st_size, a.data(), [&](qint64 at, const char* p, qint64 n) {
        if (!readAt(out, b.data(), n, at)) return false;
        same = std::memcmp(p, b.constData(), size_t(n)) == 0;
        return same;
    });
    struct stat cs {};
    if (!same || (::fstat(out, &cs) == 0 && cs.st_size != st.st_size))
        return no(QString("a sparse copy of %1 did not read back equal to it, which is left as it was").arg(path));
    if (!read) return no(QString("cannot read back a sparse copy of %1: %2").arg(path, strerror(errno)));

    const struct timespec times[2] = {st.st_atimespec, st.st_mtimespec};
    ::futimens(out, times);
    if (::fstat(out, &cs) != 0) return no(QString("cannot read back a sparse copy of %1: %2").arg(path, strerror(errno)));
    if (::rename(copy.constData(), file.constData()) != 0)
        return no(QString("cannot replace %1: %2").arg(path, strerror(errno)));
    drop.dismiss();
    s.after = qint64(cs.st_blocks) * 512;
    s.ok = true;
#else
    s.error = "sparse copies need macOS (APFS clones)";
#endif
    return s;
}

} // namespace files
