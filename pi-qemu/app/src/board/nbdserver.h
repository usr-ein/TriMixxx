#pragma once
// A read-only NBD server on a Unix socket, for QEMU's own NBD client: how a
// FatVolume reaches the emulated Pi as a USB stick. It runs on a thread of its
// own and serves one connection at a time (QEMU opens one per drive), so a read
// that waits on a slow or sleeping disk never holds up the board's event loop,
// which times the S3's MIDI. Writes are refused: the folder is only ever read.

#include <QString>

#include <atomic>
#include <memory>
#include <thread>

class FatVolume;

class NbdServer {
public:
    static constexpr const char* kExport = "stick";

    // bytesPerSecond: serve reads no faster than this, like a slow stick (0: no limit).
    NbdServer(std::shared_ptr<const FatVolume> volume, QString socketPath, qint64 bytesPerSecond = 0);
    ~NbdServer(); // stops it

    bool start(QString* error);
    void stop();
    QString socketPath() const { return m_path; }

private:
    void run();
    void serve(int fd);

    std::shared_ptr<const FatVolume> m_volume;
    QString           m_path;
    qint64            m_rate = 0;
    int               m_listen = -1;
    std::atomic<int>  m_conn{-1};
    std::atomic<bool> m_stop{false};
    std::thread       m_thread;
};
