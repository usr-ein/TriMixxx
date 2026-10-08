#pragma once
// A read-only NBD server on a Unix socket, for QEMU's own NBD client: how a
// FatVolume reaches the emulated Pi as a USB stick. It runs on a thread of its
// own and serves one connection at a time (QEMU opens one per drive), so a read
// that waits on a slow or sleeping disk never holds up the board's event loop,
// which times the S3's MIDI. Writes are refused: the folder is only ever read.
//
// It also counts what the Pi reads, file by file, which is how to see what a
// deck does with a stick: what a browse reads, what a load reads, what was read
// in the background while nobody asked. A slow stick is QEMU's business (the
// stick's throttle group, see Sticks), not this server's.

#include <QElapsedTimer>
#include <QHash>
#include <QString>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

class FatVolume;

class NbdServer {
public:
    static constexpr const char* kExport = "stick";

    NbdServer(std::shared_ptr<const FatVolume> volume, QString socketPath);
    ~NbdServer(); // stops it

    bool start(QString* error);
    void stop();
    QString socketPath() const { return m_path; }

    // What the Pi has read since the stick went in, or since the last reset:
    // the total, then the files and regions that took most of it.
    QString readsReport(int top = 25) const;
    void resetReads();

private:
    void run();
    void serve(int fd);
    void count(quint64 offset, quint32 length);

    std::shared_ptr<const FatVolume> m_volume;
    QString           m_path;
    int               m_listen = -1;
    std::atomic<int>  m_conn{-1};
    std::atomic<bool> m_stop{false};
    std::thread       m_thread;

    struct Read {
        quint64 bytes = 0;
        int     count = 0;
    };
    mutable std::mutex   m_readsLock;
    QHash<QString, Read> m_reads; // what was read -> how much
    QElapsedTimer        m_since;
};
