#include "util/filelock.h"

#include "util/fail.h"
#include "util/print.h"
#include "util/process.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace {

int open(const QString& file) {
    QDir().mkpath(QFileInfo(file).absolutePath());
    const int fd = ::open(QFile::encodeName(file).constData(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
    if (fd < 0) fail("cannot open " + file);
    return fd;
}

int op(FileLock::Mode mode) { return mode == FileLock::Exclusive ? LOCK_EX : LOCK_SH; }

} // namespace

FileLock::FileLock(const QString& file, Mode mode, const QString& waiting) : m_fd(open(file)) {
    if (::flock(m_fd, op(mode) | LOCK_NB) == 0) return;
    if (!waiting.isEmpty()) print() << waiting << "\n";
    // A blocking flock would not see ^C: polled, so the wait can be stopped.
    while (::flock(m_fd, op(mode) | LOCK_NB) != 0) {
        try {
            proc::sleep(250);
        } catch (...) {
            ::close(m_fd);
            m_fd = -1;
            throw;
        }
    }
}

FileLock FileLock::tryTake(const QString& file, Mode mode) {
    FileLock l;
    l.m_fd = open(file);
    if (::flock(l.m_fd, op(mode) | LOCK_NB) != 0) {
        ::close(l.m_fd);
        l.m_fd = -1;
    }
    return l;
}

FileLock::~FileLock() {
    if (m_fd >= 0) ::close(m_fd); // and with it the lock
}

bool lockedNow(const QString& file) {
    if (!QFileInfo::exists(file)) return false;
    return !FileLock::tryTake(file, FileLock::Shared).held();
}
