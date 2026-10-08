#include "board/link.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

// Each socket's queue: about 900 full-size frames, a few hundred milliseconds
// of a track being copied from one deck to another.
constexpr int kQueue = 2 << 20;
constexpr int kMaxFrame = 65536;
constexpr qint64 kRescanMs = 1000; // a deck that joins is heard from within a second

bool address(const QByteArray& path, sockaddr_un* a) {
    *a = sockaddr_un{};
    if (size_t(path.size()) >= sizeof(a->sun_path)) return false;
    a->sun_family = AF_UNIX;
    std::memcpy(a->sun_path, path.constData(), size_t(path.size()));
    return true;
}

void prepare(int fd) {
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    const int queue = kQueue, frame = kMaxFrame;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &queue, sizeof(queue));
    // A Unix datagram socket's send buffer is the largest datagram it can
    // send: 2 KB by default on macOS (net.local.dgram.maxdgram).
    ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &frame, sizeof(frame));
}

// Whether a board holds the socket at `path`: a datagram socket connects only
// to a bound one, so a file a crashed board left behind refuses.
bool held(const QByteArray& path) {
    sockaddr_un a;
    if (!address(path, &a)) return false;
    const int s = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    if (s < 0) return false;
    const bool ok = ::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0;
    ::close(s);
    return ok;
}

QString errnoText() { return QString::fromLocal8Bit(strerror(errno)); }

// A socket bound at `path`, refused if a live board already holds it.
int bindMember(const QString& path, QString* error) {
    const QByteArray p = QFile::encodeName(path);
    sockaddr_un a;
    if (!address(p, &a)) {
        *error = path + ": too long for a socket's path (macOS allows 103 bytes)";
        return -1;
    }
    if (held(p)) {
        *error = QFileInfo(path).completeBaseName() + " is already on this link (" + path + ")";
        return -1;
    }
    ::unlink(p.constData());
    const int s = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    if (s < 0 || ::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) < 0) {
        *error = "cannot join the link at " + path + ": " + errnoText();
        if (s >= 0) ::close(s);
        return -1;
    }
    prepare(s);
    return s;
}

QString memberOf(const char* path) {
    const QString name = QFileInfo(QFile::decodeName(path)).fileName();
    return name.endsWith(".sock") ? name.chopped(5) : name;
}

QString macText(const unsigned char* m) {
    return QString::asprintf("%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
}

} // namespace

namespace links {

QString root() {
    const QString env = qEnvironmentVariable("PI_QEMU_LINKS");
    return env.isEmpty() ? QDir::home().filePath(".pi-qemu/links") : env;
}

QString dir(const QString& net) { return root() + "/" + net; }

bool validName(const QString& s) {
    static const QRegularExpression ok("^[a-zA-Z0-9][a-zA-Z0-9_-]*$");
    return ok.match(s).hasMatch();
}

// Locally administered, in pi-qemu's 02:54:4d ("TM") block, and even, so the
// management NIC's MAC (one above) shares all but the last byte.
QString macFor(const QString& deck) {
    const QByteArray h = QCryptographicHash::hash(deck.toUtf8(), QCryptographicHash::Sha256);
    const unsigned char m[6] = {0x02, 0x54, 0x4d, uchar(h[0]), uchar(h[1]), uchar(uchar(h[2]) & 0xfe)};
    return macText(m);
}

QStringList nets() {
    return QDir(root()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}

QList<Member> members(const QString& net) {
    QList<Member> out;
    const QDir d(dir(net));
    for (const QString& f : d.entryList(QStringList{"*.sock"}, QDir::System | QDir::Hidden | QDir::Files, QDir::Name)) {
        Member m;
        m.name = f.chopped(5);
        m.listener = m.name.startsWith('.');
        m.live = held(QFile::encodeName(d.filePath(f)));
        out << m;
    }
    return out;
}

void forget(const QString& net, const QString& member) {
    const QByteArray p = QFile::encodeName(dir(net) + "/" + member + ".sock");
    if (!held(p)) ::unlink(p.constData());
}

KeepAlive keepAlive(const char* f, size_t n) {
    KeepAlive k;
    const auto* b = reinterpret_cast<const unsigned char*>(f);
    if (n < 14 + 20 + 8 || b[12] != 0x08 || b[13] != 0x00) return k; // IPv4
    const size_t ip = 14, ihl = size_t(b[ip] & 0x0f) * 4;
    if ((b[ip] >> 4) != 4 || ihl < 20 || b[ip + 9] != 17) return k; // UDP
    if (((b[ip + 6] & 0x1f) | b[ip + 7]) != 0) return k;             // a fragment's tail
    const size_t udp = ip + ihl, p = udp + 8;
    if (n < p + 0x30 || ((b[udp + 2] << 8) | b[udp + 3]) != 50000) return k;
    if (std::memcmp(b + p, "Qspt1WmJOL", 10) != 0 || b[p + 0x0a] != 0x06) return k;
    k.number = b[p + 0x24];
    k.name = QString::fromLatin1(f + p + 0x0c, int(strnlen(f + p + 0x0c, 20)));
    k.mac = macText(b + p + 0x26);
    k.ip = QString("%1.%2.%3.%4").arg(b[p + 0x2c]).arg(b[p + 0x2d]).arg(b[p + 0x2e]).arg(b[p + 0x2f]);
    return k;
}

} // namespace links

// ---- a board's port on the link ---------------------------------------------------

Link::Link(QString net, QString member) : m_net(std::move(net)), m_member(std::move(member)) {}

Link::~Link() { close(); }

QString Link::path() const { return links::dir(m_net) + "/" + m_member + ".sock"; }

bool Link::open(QString* error) {
    if (!links::validName(m_net)) { *error = "a link's name is letters, digits, - and _: not " + m_net; return false; }
    if (!links::validName(m_member)) { *error = "a member's name is letters, digits, - and _: not " + m_member; return false; }
    if (!QDir().mkpath(links::dir(m_net))) { *error = "cannot make " + links::dir(m_net); return false; }
    m_sock = bindMember(path(), error);
    if (m_sock < 0) return false;
    int pair[2];
    if (::socketpair(AF_UNIX, SOCK_DGRAM, 0, pair) < 0) {
        *error = "no socketpair for the Pi's eth0: " + errnoText();
        close();
        return false;
    }
    m_qemu = pair[0], m_pi = pair[1];
    prepare(m_qemu);
    prepare(m_pi);
    m_stop = false;
    m_thread = std::thread([this] { run(); });
    return true;
}

void Link::close() {
    if (m_thread.joinable()) {
        m_stop = true;
        m_thread.join();
    }
    for (int* fd : {&m_qemu, &m_pi, &m_sock})
        if (*fd >= 0) { ::close(*fd); *fd = -1; }
    // Only a socket of ours: another board may have taken the name since.
    const QByteArray p = QFile::encodeName(path());
    if (!held(p)) ::unlink(p.constData());
}

void Link::drain() {
    if (m_qemu < 0) return;
    char frame[kMaxFrame];
    while (::recv(m_qemu, frame, sizeof(frame), 0) > 0) {}
}

// Plugging in, even again, listens afresh for what the deck announces.
void Link::setPlugged(bool plugged) {
    if (plugged) {
        std::lock_guard<std::mutex> l(m_heardLock);
        m_heard = Heard();
    }
    m_plugged = plugged;
}

Link::Heard Link::heard() const {
    std::lock_guard<std::mutex> l(m_heardLock);
    return m_heard;
}

void Link::run() {
    std::vector<char> buf(kMaxFrame);
    while (!m_stop) {
        if (QDateTime::currentMSecsSinceEpoch() - m_scanned >= kRescanMs) refreshMembers();
        pollfd p[2] = {{m_pi, POLLIN, 0}, {m_sock, POLLIN, 0}};
        if (::poll(p, 2, 200) <= 0) continue; // a look at m_stop five times a second
        // A burst from each side in turn, so neither waits on the other.
        for (int i = 0; i < 64 && (p[0].revents & POLLIN); i++) {
            const ssize_t n = ::recv(m_pi, buf.data(), buf.size(), 0);
            if (n <= 0) break;
            fromPi(buf.data(), size_t(n));
        }
        for (int i = 0; i < 64 && (p[1].revents & POLLIN); i++) {
            sockaddr_un from{};
            socklen_t len = sizeof(from);
            const ssize_t n = ::recvfrom(m_sock, buf.data(), buf.size(), 0, reinterpret_cast<sockaddr*>(&from), &len);
            if (n <= 0) break;
            const bool named = len > socklen_t(offsetof(sockaddr_un, sun_path)) && from.sun_path[0];
            fromMember(buf.data(), size_t(n), named ? std::string(from.sun_path) : std::string());
        }
    }
}

void Link::refreshMembers() {
    m_scanned = QDateTime::currentMSecsSinceEpoch();
    const QByteArray d = QFile::encodeName(links::dir(m_net));
    const std::string self = QFile::encodeName(path()).toStdString();
    std::vector<std::string> members, listeners;
    if (DIR* dir = ::opendir(d.constData())) {
        while (const dirent* e = ::readdir(dir)) {
            const size_t n = strlen(e->d_name);
            if (n <= 5 || std::strcmp(e->d_name + n - 5, ".sock") != 0) continue;
            const std::string p = d.toStdString() + "/" + e->d_name;
            if (p == self) continue;
            (e->d_name[0] == '.' ? listeners : members).push_back(p);
        }
        ::closedir(dir);
    }
    std::sort(members.begin(), members.end());
    m_members.swap(members);
    m_listeners.swap(listeners);
}

// False when nobody holds that socket any more: the member is gone.
bool Link::sendTo(const std::string& path, const char* frame, size_t n) {
    sockaddr_un a{};
    if (path.size() >= sizeof(a.sun_path)) return false;
    a.sun_family = AF_UNIX;
    std::memcpy(a.sun_path, path.data(), path.size());
    if (::sendto(m_sock, frame, n, 0, reinterpret_cast<sockaddr*>(&a), sizeof(a)) >= 0) return true;
    if (errno == ECONNREFUSED || errno == ENOENT || errno == ENOTSOCK) return false;
    m_dropped++; // its queue is full: dropped, as a busy switch drops it
    return true;
}

void Link::fromPi(const char* frame, size_t n) {
    if (!m_plugged || n < 14) return;
    m_out++;
    const links::KeepAlive k = links::keepAlive(frame, n);
    if (k.number) {
        std::lock_guard<std::mutex> l(m_heardLock);
        m_heard = Heard{k.number, k.ip, k.mac};
    }
    Mac dst;
    std::memcpy(dst.data(), frame, 6);
    const auto known = (dst[0] & 1) ? m_where.end() : m_where.find(dst);
    if (known != m_where.end()) {
        if (!sendTo(known->second, frame, n)) {
            const std::string gone = known->second;
            std::erase_if(m_where, [&](const auto& e) { return e.second == gone; });
        }
    } else {
        for (const std::string& m : m_members) sendTo(m, frame, n);
    }
    for (const std::string& l : m_listeners) sendTo(l, frame, n);
}

void Link::fromMember(const char* frame, size_t n, const std::string& from) {
    if (n < 14) return;
    Mac src;
    std::memcpy(src.data(), frame + 6, 6);
    if (!from.empty() && !(src[0] & 1)) m_where[src] = from;
    if (!m_plugged) return;
    m_in++;
    if (::send(m_pi, frame, n, 0) < 0) m_dropped++;
}

// ---- a listener ----------------------------------------------------------------------

LinkTap::LinkTap(QString net) : m_net(std::move(net)) {}

LinkTap::~LinkTap() {
    if (m_sock >= 0) ::close(m_sock);
    if (!m_path.isEmpty()) ::unlink(QFile::encodeName(m_path).constData());
}

bool LinkTap::open(QString* error) {
    if (!links::validName(m_net)) { *error = "a link's name is letters, digits, - and _: not " + m_net; return false; }
    if (!QDir().mkpath(links::dir(m_net))) { *error = "cannot make " + links::dir(m_net); return false; }
    const QString path = links::dir(m_net) + QString("/.tap-%1.sock").arg(::getpid());
    m_sock = bindMember(path, error);
    if (m_sock < 0) return false;
    m_path = path;
    return true;
}

bool LinkTap::next(std::vector<char>* frame, QString* from, int timeoutMs) {
    pollfd p{m_sock, POLLIN, 0};
    if (::poll(&p, 1, timeoutMs) <= 0) return false;
    frame->resize(kMaxFrame);
    sockaddr_un a{};
    socklen_t len = sizeof(a);
    const ssize_t n = ::recvfrom(m_sock, frame->data(), frame->size(), 0, reinterpret_cast<sockaddr*>(&a), &len);
    if (n < 0) return false;
    frame->resize(size_t(n));
    if (from) *from = len > socklen_t(offsetof(sockaddr_un, sun_path)) ? memberOf(a.sun_path) : QString();
    return true;
}
