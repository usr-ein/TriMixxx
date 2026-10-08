#include "board/nbdserver.h"

#include "board/fatvolume.h"

#include <QFile>
#include <QtEndian>

#include <chrono>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

// The protocol: https://github.com/NetworkBlockDevice/nbd/blob/master/doc/proto.md
// Fixed newstyle negotiation, simple replies only: QEMU asks for structured
// replies and extended headers, hears they are not supported, and does without.
namespace {

constexpr quint64 kNbdMagic = 0x4e42444d41474943ull;   // "NBDMAGIC"
constexpr quint64 kOptMagic = 0x49484156454F5054ull;   // "IHAVEOPT"
constexpr quint64 kReplyMagic = 0x0003e889045565a9ull; // an option's reply
constexpr quint32 kRequestMagic = 0x25609513;
constexpr quint32 kSimpleReplyMagic = 0x67446698;

constexpr quint16 kFixedNewstyle = 1, kNoZeroes = 2;
constexpr quint32 kOptExportName = 1, kOptAbort = 2, kOptList = 3, kOptInfo = 6, kOptGo = 7;
constexpr quint32 kRepAck = 1, kRepServer = 2, kRepInfo = 3, kRepErrUnsup = 0x80000001, kRepErrInvalid = 0x80000003;
constexpr quint16 kInfoExport = 0, kInfoBlockSize = 3;
constexpr quint16 kFlagHasFlags = 1, kFlagReadOnly = 2, kFlagCanMultiConn = 1 << 8;
constexpr quint16 kCmdRead = 0, kCmdWrite = 1, kCmdDisc = 2, kCmdFlush = 3, kCmdCache = 5;
constexpr quint32 kEperm = 1, kEio = 5, kEinval = 22;
constexpr quint32 kMaxRead = 32u << 20;

bool readAll(int fd, void* buf, size_t n) {
    char* p = static_cast<char*>(buf);
    while (n > 0) {
        const ssize_t k = ::read(fd, p, n);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return false;
        p += k;
        n -= size_t(k);
    }
    return true;
}

bool writeAll(int fd, const void* buf, size_t n) {
    const char* p = static_cast<const char*>(buf);
    while (n > 0) {
        const ssize_t k = ::write(fd, p, n);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return false;
        p += k;
        n -= size_t(k);
    }
    return true;
}

// Big-endian, as everything on the wire is.
struct Out {
    std::vector<char> b;
    Out& u16(quint16 v) { char t[2]; qToBigEndian(v, t); b.insert(b.end(), t, t + 2); return *this; }
    Out& u32(quint32 v) { char t[4]; qToBigEndian(v, t); b.insert(b.end(), t, t + 4); return *this; }
    Out& u64(quint64 v) { char t[8]; qToBigEndian(v, t); b.insert(b.end(), t, t + 8); return *this; }
    Out& raw(const char* p, size_t n) { b.insert(b.end(), p, p + n); return *this; }
    bool send(int fd) const { return writeAll(fd, b.data(), b.size()); }
};

bool optionReply(int fd, quint32 option, quint32 type, const Out& data = {}) {
    Out o;
    o.u64(kReplyMagic).u32(option).u32(type).u32(quint32(data.b.size())).raw(data.b.data(), data.b.size());
    return o.send(fd);
}

} // namespace

NbdServer::NbdServer(std::shared_ptr<const FatVolume> volume, QString socketPath, qint64 bytesPerSecond)
    : m_volume(std::move(volume)), m_path(std::move(socketPath)), m_rate(bytesPerSecond) {}

NbdServer::~NbdServer() { stop(); }

bool NbdServer::start(QString* error) {
    const QByteArray path = QFile::encodeName(m_path);
    sockaddr_un addr{};
    if (size_t(path.size()) >= sizeof(addr.sun_path)) { *error = m_path + ": too long for a socket's path"; return false; }
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.constData(), size_t(path.size()));
    ::unlink(path.constData());
    m_listen = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_listen < 0 || ::bind(m_listen, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 || ::listen(m_listen, 4) < 0) {
        *error = QString("cannot serve the stick on %1: %2").arg(m_path, QString::fromLocal8Bit(strerror(errno)));
        if (m_listen >= 0) ::close(m_listen);
        m_listen = -1;
        return false;
    }
    m_thread = std::thread([this] { run(); });
    return true;
}

void NbdServer::stop() {
    if (!m_thread.joinable()) return;
    m_stop = true;
    const int conn = m_conn.load();
    if (conn >= 0) ::shutdown(conn, SHUT_RDWR); // wakes a read waiting on QEMU
    m_thread.join();
    ::close(m_listen);
    m_listen = -1;
    ::unlink(QFile::encodeName(m_path).constData());
}

void NbdServer::run() {
    while (!m_stop) {
        pollfd p{m_listen, POLLIN, 0};
        if (::poll(&p, 1, 200) <= 0) continue; // a look at m_stop five times a second
        const int fd = ::accept(m_listen, nullptr, nullptr);
        if (fd < 0) continue;
        const int on = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
        m_conn = fd;
        if (!m_stop) serve(fd);
        m_conn = -1;
        ::close(fd);
    }
}

void NbdServer::serve(int fd) {
    const quint64 size = m_volume->size();
    const quint16 flags = kFlagHasFlags | kFlagReadOnly | kFlagCanMultiConn;

    // Negotiation.
    if (!Out().u64(kNbdMagic).u64(kOptMagic).u16(kFixedNewstyle | kNoZeroes).send(fd)) return;
    quint32 clientFlags;
    if (!readAll(fd, &clientFlags, 4)) return;
    const bool noZeroes = qFromBigEndian(clientFlags) & kNoZeroes;
    for (;;) {
        char head[16];
        if (!readAll(fd, head, 16) || qFromBigEndian<quint64>(head) != kOptMagic) return;
        const quint32 option = qFromBigEndian<quint32>(head + 8), length = qFromBigEndian<quint32>(head + 12);
        if (length > 65536) return;
        std::vector<char> data(length);
        if (length && !readAll(fd, data.data(), length)) return;

        if (option == kOptExportName) { // the old way in: no reply header, straight to transmission
            Out o;
            o.u64(size).u16(flags);
            if (!noZeroes) o.raw(std::vector<char>(124, 0).data(), 124);
            if (!o.send(fd)) return;
            break;
        }
        if (option == kOptGo || option == kOptInfo) {
            if (length < 6) { if (!optionReply(fd, option, kRepErrInvalid)) return; continue; }
            const quint32 nameLength = qFromBigEndian<quint32>(data.data());
            if (quint64(nameLength) + 6 > length) { if (!optionReply(fd, option, kRepErrInvalid)) return; continue; }
            const quint16 requests = qFromBigEndian<quint16>(data.data() + 4 + nameLength);
            bool blockSize = false;
            for (quint32 i = 0; i < requests && 4 + nameLength + 2 + 2 * i + 2 <= length; i++)
                blockSize |= qFromBigEndian<quint16>(data.data() + 4 + nameLength + 2 + 2 * i) == kInfoBlockSize;
            // Whatever the export is called: there is only the one.
            if (!optionReply(fd, option, kRepInfo, Out().u16(kInfoExport).u64(size).u16(flags))) return;
            if (blockSize && !optionReply(fd, option, kRepInfo, Out().u16(kInfoBlockSize).u32(1).u32(4096).u32(kMaxRead))) return;
            if (!optionReply(fd, option, kRepAck)) return;
            if (option == kOptGo) break;
            continue;
        }
        if (option == kOptList) {
            const quint32 n = quint32(std::strlen(kExport));
            if (!optionReply(fd, option, kRepServer, Out().u32(n).raw(kExport, n)) || !optionReply(fd, option, kRepAck)) return;
            continue;
        }
        if (option == kOptAbort) { optionReply(fd, option, kRepAck); return; }
        if (!optionReply(fd, option, kRepErrUnsup)) return; // structured replies, metadata contexts, TLS, ...
    }

    // Transmission.
    using Clock = std::chrono::steady_clock;
    Clock::time_point free = Clock::now(); // when a slow stick can start on the next read
    std::vector<char> buf;
    while (!m_stop) {
        char req[28];
        if (!readAll(fd, req, 28) || qFromBigEndian<quint32>(req) != kRequestMagic) return;
        const quint16 type = qFromBigEndian<quint16>(req + 6);
        const quint64 handle = qFromBigEndian<quint64>(req + 8), offset = qFromBigEndian<quint64>(req + 16);
        const quint32 length = qFromBigEndian<quint32>(req + 24);
        auto reply = [&](quint32 error) { return Out().u32(kSimpleReplyMagic).u32(error).u64(handle).send(fd); };

        if (type == kCmdDisc) return;
        if (type == kCmdRead) {
            if (length > kMaxRead || offset > size || length > size - offset) { if (!reply(kEinval)) return; continue; }
            buf.resize(length);
            QString error;
            if (!m_volume->read(offset, buf.data(), length, &error)) {
                qWarning("stick: %s", qPrintable(error));
                if (!reply(kEio)) return;
                continue;
            }
            if (m_rate > 0) { // a slow stick: each read takes its time, after the one before it
                free = std::max(free, Clock::now()) + std::chrono::microseconds(quint64(length) * 1000000 / quint64(m_rate));
                std::this_thread::sleep_until(free);
            }
            if (!reply(0) || !writeAll(fd, buf.data(), length)) return;
            continue;
        }
        if (type == kCmdWrite) { // refused, after taking its payload off the wire
            buf.resize(length);
            if (length > (64u << 20) || !readAll(fd, buf.data(), length)) return;
            if (!reply(kEperm)) return;
            continue;
        }
        if (type == kCmdFlush || type == kCmdCache) { if (!reply(0)) return; continue; }
        if (!reply(type == 4 || type == 6 ? kEperm : kEinval)) return; // trim, write zeroes; anything else
    }
}
