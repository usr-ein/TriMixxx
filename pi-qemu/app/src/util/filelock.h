#pragma once
// An flock(2) on a file, held while this lives: shared by readers, or
// exclusive for one writer. The kernel lets go of it with the process,
// however that ends, so a command that dies leaves nothing locked.

#include <QString>

class FileLock {
public:
    enum Mode { Shared, Exclusive };

    // Waits until it has it, printing `waiting` once if it has to wait.
    FileLock(const QString& file, Mode mode, const QString& waiting = {});
    // Only if nobody holds it the other way now; held() says whether.
    static FileLock tryTake(const QString& file, Mode mode);
    ~FileLock();

    FileLock(FileLock&& other) noexcept : m_fd(other.m_fd) { other.m_fd = -1; }
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    FileLock& operator=(FileLock&&) = delete;

    bool held() const { return m_fd >= 0; }

private:
    FileLock() = default;
    int m_fd = -1;
};

// Whether a process holds `file` exclusively now (this one too, on another
// descriptor): a shared lock is tried, and let go at once.
bool lockedNow(const QString& file);
