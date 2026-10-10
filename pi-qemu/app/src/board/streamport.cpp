#include "board/streamport.h"

#include <QFile>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

bool StreamFramer::feed(const char* data, size_t n, const std::function<void(const char*, size_t)>& frame) {
    m_buf.insert(m_buf.end(), data, data + n);
    size_t at = 0;
    while (m_buf.size() - at >= 4) {
        const auto* b = reinterpret_cast<const unsigned char*>(m_buf.data() + at);
        const size_t len = (size_t(b[0]) << 24) | (size_t(b[1]) << 16) | (size_t(b[2]) << 8) | b[3];
        if (len == 0 || len > kMaxFrame) return false;
        if (m_buf.size() - at - 4 < len) break;
        frame(m_buf.data() + at + 4, len);
        at += 4 + len;
    }
    m_buf.erase(m_buf.begin(), m_buf.begin() + std::ptrdiff_t(at));
    return true;
}

std::vector<char> StreamFramer::encode(const char* frame, size_t n) {
    std::vector<char> out(4 + n);
    out[0] = char(n >> 24), out[1] = char(n >> 16), out[2] = char(n >> 8), out[3] = char(n);
    std::memcpy(out.data() + 4, frame, n);
    return out;
}

StreamPort::StreamPort(QString path, int datagramFd) : m_path(std::move(path)), m_dgram(datagramFd) {}

StreamPort::~StreamPort() { close(); }

bool StreamPort::open(QString* error) {
    const QByteArray p = QFile::encodeName(m_path);
    sockaddr_un a{};
    if (size_t(p.size()) >= sizeof(a.sun_path)) {
        *error = m_path + ": too long for a socket's path (macOS allows 103 bytes)";
        return false;
    }
    a.sun_family = AF_UNIX;
    std::memcpy(a.sun_path, p.constData(), size_t(p.size()));
    ::unlink(p.constData());
    m_listen = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_listen < 0 || ::bind(m_listen, reinterpret_cast<sockaddr*>(&a), sizeof(a)) < 0 || ::listen(m_listen, 1) < 0) {
        *error = "cannot listen at " + m_path + ": " + QString::fromLocal8Bit(strerror(errno));
        close();
        return false;
    }
    ::fcntl(m_listen, F_SETFD, FD_CLOEXEC);
    m_stop = false;
    m_thread = std::thread([this] { run(); });
    return true;
}

void StreamPort::close() {
    if (m_thread.joinable()) {
        m_stop = true;
        m_thread.join();
    }
    if (m_listen >= 0) {
        ::close(m_listen);
        m_listen = -1;
        ::unlink(QFile::encodeName(m_path).constData());
    }
}

void StreamPort::drop(int* fd) {
    if (*fd >= 0) ::close(*fd);
    *fd = -1;
    m_connected = false;
}

void StreamPort::run() {
    std::vector<char> buf(StreamFramer::kMaxFrame + 4);
    StreamFramer framer;
    int client = -1;
    while (!m_stop) {
        pollfd p[2] = {{client >= 0 ? client : m_listen, POLLIN, 0}, {m_dgram, POLLIN, 0}};
        if (::poll(p, 2, 200) <= 0) continue; // a look at m_stop five times a second
        if (client < 0 && (p[0].revents & POLLIN)) {
            client = ::accept(m_listen, nullptr, nullptr);
            if (client >= 0) {
                ::fcntl(client, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
                const int on = 1; // a gone emulator is an error to read, not a signal
                ::setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
                // A stalled emulator costs the link a second, not the thread.
                const timeval second{1, 0};
                ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &second, sizeof(second));
                framer.reset();
                m_connected = true;
            }
        } else if (client >= 0 && (p[0].revents & (POLLIN | POLLHUP | POLLERR))) {
            const ssize_t n = ::recv(client, buf.data(), buf.size(), 0);
            const bool ok = n > 0 && framer.feed(buf.data(), size_t(n), [&](const char* f, size_t len) {
                if (::send(m_dgram, f, len, 0) >= 0) m_in++;
            });
            if (!ok) drop(&client); // gone, or a broken stream: the next connection starts afresh
        }
        // The link's frames: to the emulator if one is connected, else nowhere.
        for (int i = 0; i < 64 && (p[1].revents & POLLIN); i++) {
            const ssize_t n = ::recv(m_dgram, buf.data(), buf.size(), MSG_DONTWAIT);
            if (n <= 0) break;
            if (client < 0) continue;
            const std::vector<char> out = StreamFramer::encode(buf.data(), size_t(n));
            size_t sent = 0;
            while (sent < out.size()) {
                const ssize_t w = ::send(client, out.data() + sent, out.size() - sent, 0);
                if (w <= 0) break;
                sent += size_t(w);
            }
            if (sent == out.size()) m_out++;
            else if (sent > 0) { drop(&client); break; } // half a frame: the stream is broken
            // else lost whole, as a busy switch loses it; the stream is intact
        }
    }
    drop(&client);
}
