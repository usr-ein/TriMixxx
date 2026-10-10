#pragma once
// A QEMU stream netdev on a link: the frames of `-netdev stream` (and of the
// older `-netdev socket`), each a 32-bit big-endian length and then the
// frame, put one per datagram on a datagram socket -- Link's qemuFd(), the
// end a deck's QEMU would hold itself.
//
// For an emulator whose QEMU pi-qemu does not start, so cannot hand a file
// descriptor: an emulated CDJ (cdj/cdj.h), whose launcher connects its NIC
// to a Unix stream socket here (nxs_vm --link-hub unix:PATH). One emulator
// at a time: a second connection waits until the first has gone. Frames
// from the link while none is connected are dropped, as a cable to a
// switched-off player drops them. A stream it has to drop (a broken length,
// half a frame after a stall) is said on stderr: the emulator's NIC does not
// connect again.

#include <QString>

#include <atomic>
#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

// The stream's framing alone, for the port and its tests: bytes in, whole
// frames out. A length of 0 or over kMaxFrame is a broken stream.
class StreamFramer {
public:
    static constexpr size_t kMaxFrame = 65535;
    // Calls `frame` for each whole frame in what has arrived so far; false
    // on a broken stream (the rest is not to be trusted).
    bool feed(const char* data, size_t n, const std::function<void(const char*, size_t)>& frame);
    void reset() { m_buf.clear(); }
    static std::vector<char> encode(const char* frame, size_t n); // length, then frame

private:
    std::vector<char> m_buf;
};

class StreamPort {
public:
    StreamPort(QString path, int datagramFd);
    ~StreamPort(); // stops it, and removes its socket
    StreamPort(const StreamPort&) = delete;
    StreamPort& operator=(const StreamPort&) = delete;

    bool open(QString* error); // listens at path, and starts the thread
    void close();

    bool    connected() const { return m_connected; }
    quint64 framesIn() const { return m_in; }   // stream -> datagram
    quint64 framesOut() const { return m_out; } // datagram -> stream

private:
    void run();
    void drop(int* fd, const char* why); // why: said in the log; null at shutdown

    QString m_path;
    int m_dgram = -1, m_listen = -1;
    std::atomic<bool> m_stop{false}, m_connected{false};
    std::atomic<quint64> m_in{0}, m_out{0};
    std::thread m_thread;
};
