#include "board/sticks.h"

#include "board/fatvolume.h"
#include "board/machine.h"
#include "util/fail.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <fcntl.h>
#include <QJsonArray>
#include <QJsonObject>
#include <QCoreApplication>
#include <QPointer>
#include <QProcess>
#include <QTimer>

#include <spawn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace {

QByteArray run(const QString& program, const QStringList& args) {
    QProcess p;
    p.start(program, args);
    p.waitForFinished(15000);
    return p.readAllStandardOutput();
}

// diskutil -plist output, decoded by Qt's own property-list-free route: ask for
// the few keys we need with plutil, which macOS ships.
QString plistValue(const QByteArray& plist, const QString& key) {
    QProcess p;
    p.start("plutil", {"-extract", key, "raw", "-o", "-", "-"});
    p.write(plist);
    p.closeWriteChannel();
    p.waitForFinished(5000);
    return p.exitCode() == 0 ? QString::fromUtf8(p.readAllStandardOutput()).trimmed() : QString();
}

} // namespace

Sticks::Sticks(Machine* m, QString imagesDir, QObject* parent)
    : QObject(parent), m_machine(m), m_imagesDir(std::move(imagesDir)) {
    connect(m, &Machine::poweredOff, this, [this] { if (m_fdMon >= 0) { ::close(m_fdMon); m_fdMon = -1; } });
}

QVector<HostStick> Sticks::list() {
    QVector<HostStick> out;
    QByteArray all = run("diskutil", {"list", "-plist", "external", "physical"});
    for (int i = 0;; i++) {
        QString disk = plistValue(all, QString("WholeDisks.%1").arg(i));
        if (disk.isEmpty()) break;
        QByteArray info = run("diskutil", {"info", "-plist", disk});
        if (plistValue(info, "BusProtocol") != "USB") continue;
        HostStick s;
        s.id = disk;
        s.name = plistValue(info, "MediaName");
        s.size = plistValue(info, "TotalSize").toLongLong();
        s.inserted = m_inserted.contains(disk);
        out << s;
    }
    QStringList images;
    for (const QFileInfo& f : QDir(m_imagesDir).entryInfoList({"*.img", "*.stick"}, QDir::Files, QDir::Name))
        images << f.absoluteFilePath();
    for (auto it = m_inserted.begin(); it != m_inserted.end(); ++it)
        if (!it.key().startsWith("disk") && !images.contains(it.key())) images << it.key();
    for (const QString& path : images) {
        const QFileInfo f(path);
        const bool folder = f.isDir() || f.suffix() == "stick";
        out << HostStick{path, (f.isDir() ? f.fileName() : f.completeBaseName()) + (folder ? " (folder)" : " (image)"),
                         folder ? 0 : f.size(), m_inserted.contains(path)};
    }
    return out;
}

// macOS's authopen opens a device with the user's admin authorisation and
// passes the descriptor back over a socket: pi-qemu itself never runs as root.
int Sticks::openRawDisk(const QString& disk, QString* error) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) { *error = "socketpair failed"; return -1; }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, sv[1], STDOUT_FILENO);
    QByteArray path = ("/dev/r" + disk).toUtf8();
    char* argv[] = {const_cast<char*>("/usr/libexec/authopen"), const_cast<char*>("-stdoutpipe"),
                    path.data(), nullptr};
    pid_t pid;
    int rc = posix_spawn(&pid, argv[0], &fa, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(sv[1]);
    if (rc != 0) { close(sv[0]); *error = "could not start authopen"; return -1; }
    char byte;
    char ctrl[CMSG_SPACE(sizeof(int))];
    iovec iov{&byte, 1};
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = ctrl;
    msg.msg_controllen = sizeof(ctrl);
    ssize_t n = recvmsg(sv[0], &msg, 0);
    close(sv[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    cmsghdr* c = CMSG_FIRSTHDR(&msg);
    if (n <= 0 || !c || c->cmsg_type != SCM_RIGHTS) {
        *error = "not authorised to read " + disk;
        return -1;
    }
    int fd;
    memcpy(&fd, CMSG_DATA(c), sizeof(fd));
    return fd;
}

// QMP takes a descriptor alongside a command (add-fd) on its Unix socket, as
// SCM_RIGHTS. Qt's socket cannot attach one, so this is a plain socket on the
// second monitor, kept open: a descriptor set lives as long as the connection
// that brought it, and the stick needs it until it is unplugged.
bool Sticks::passFd(int fd, int fdset, QString* error) {
    // QEMU's monitor serves one client at a time, and a descriptor set lives
    // as long as the connection that brought it: so one connection, opened
    // once and kept for as long as the board runs. Reads time out, so a
    // monitor that does not answer can never freeze the window.
    auto readLine = [this] {
        QByteArray line;
        char ch;
        while (::read(m_fdMon, &ch, 1) == 1 && ch != '\n') line += ch;
        return line;
    };
    if (m_fdMon < 0) {
        int s = socket(AF_UNIX, SOCK_STREAM, 0);
        timeval tv{5, 0};
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        QByteArray path = (m_machine->runDir() + "/qmp-fd.sock").toUtf8();
        strncpy(addr.sun_path, path.constData(), sizeof(addr.sun_path) - 1);
        if (s < 0 || ::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            if (s >= 0) ::close(s);
            *error = "cannot reach QEMU's fd monitor";
            return false;
        }
        m_fdMon = s;
        if (!readLine().contains("QMP")) { ::close(m_fdMon); m_fdMon = -1; *error = "QEMU's fd monitor did not greet"; return false; }
        const char caps[] = "{\"execute\":\"qmp_capabilities\"}\n";
        (void)::write(m_fdMon, caps, sizeof(caps) - 1);
        readLine();
    }
    QByteArray cmd = QString(R"({"execute":"add-fd","arguments":{"fdset-id":%1}})" "\n").arg(fdset).toUtf8();
    iovec iov{cmd.data(), size_t(cmd.size())};
    char ctrl[CMSG_SPACE(sizeof(int))] = {};
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = ctrl;
    msg.msg_controllen = sizeof(ctrl);
    cmsghdr* c = CMSG_FIRSTHDR(&msg);
    c->cmsg_level = SOL_SOCKET;
    c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(c), &fd, sizeof(int));
    if (sendmsg(m_fdMon, &msg, 0) < 0) { *error = "could not pass the stick to QEMU"; return false; }
    QByteArray reply;
    do reply = readLine(); while (reply.contains("\"event\"")); // events go to every monitor
    if (!reply.contains("\"return\"")) { *error = "QEMU refused the stick: " + QString(reply); return false; }
    return true;
}

void Sticks::dropFdset(int fdset) {
    if (m_fdMon < 0) return;
    QByteArray cmd = QString(R"({"execute":"remove-fd","arguments":{"fdset-id":%1}})" "\n").arg(fdset).toUtf8();
    (void)::write(m_fdMon, cmd.constData(), cmd.size());
    char ch;
    while (::read(m_fdMon, &ch, 1) == 1 && ch != '\n') {}
}

// A bare name finds the stick in the sticks folder: "SAM3" -> SAM3.img,
// "SANDISK" -> SANDISK.stick.
QString Sticks::resolve(const QString& name) const {
    QString id = name;
    if (!id.startsWith("disk") && !id.startsWith("fd:") && !QFileInfo::exists(id)) {
        for (const QString& cand : {id, id + ".img", id + ".stick"})
            if (QFileInfo::exists(QDir(m_imagesDir).filePath(cand))) { id = QDir(m_imagesDir).filePath(cand); break; }
    }
    if (QFileInfo::exists(id)) id = QFileInfo(id).absoluteFilePath();
    return id;
}

// The drive under QEMU's usb-storage, read-only whatever is beneath it.
void Sticks::plug(const QString& id, int n, const QJsonObject& file, const QString& note, Done done) {
    const QString node = QString("stick%1").arg(n), dev = QString("usbstick%1").arg(n);
    auto failed = [=, this](const QString& why) {
        m_servers.erase(n);
        done(false, why);
    };
    QJsonObject bd{{"node-name", node}, {"driver", "raw"}, {"read-only", true}, {"file", file}};
    m_machine->qmp("blockdev-add", bd, [=, this](const QJsonObject& r) {
        if (r.contains("error")) { failed(r["error"].toObject()["desc"].toString()); return; }
        m_machine->qmp("device_add",
                       QJsonObject{{"driver", "usb-storage"}, {"id", dev}, {"bus", "xhci.0"}, {"drive", node}},
                       [=, this](const QJsonObject& r2) {
                           if (r2.contains("error")) {
                               m_machine->qmp("blockdev-del", QJsonObject{{"node-name", node}});
                               failed(r2["error"].toObject()["desc"].toString());
                               return;
                           }
                           m_inserted[id] = n;
                           emit changed();
                           done(true, id + " inserted" + (note.isEmpty() ? QString() : ": " + note));
                       });
    });
}

// A folder, or a .stick file naming one: a FAT32 disk made up from it, served
// by an NbdServer of its own on a socket in the run directory, which QEMU's
// NBD client reads as it would a file.
void Sticks::insertFolder(const QString& id, int n, Done done) {
    m_pending.insert(id);
    QString socket = QDir(m_machine->runDir()).filePath(QString("stick%1.nbd").arg(n));
    if (QFile::encodeName(socket).size() > 100) // past what a Unix socket's path can hold
        socket = QString("/tmp/pi-qemu-%1-stick%2.nbd").arg(QCoreApplication::applicationPid()).arg(n);
    // The walk reads every folder on the stick, which takes seconds on a disk
    // that was asleep: not on the event loop.
    QPointer<Sticks> self(this);
    std::thread([=] {
        std::shared_ptr<FatVolume> volume;
        FatSpec spec;
        QString error;
        try {
            if (QFileInfo(id).isDir()) spec.folder = id;
            else spec = FatSpec::load(id);
            volume = std::make_shared<FatVolume>(spec);
        } catch (const Failure& f) {
            error = f.message;
        }
        QMetaObject::invokeMethod(qApp, [=] {
            if (!self) return;
            self->m_pending.remove(id);
            if (!volume) { done(false, error); return; }
            if (!self->m_machine->running()) { done(false, "the Pi is not running"); return; }
            auto server = std::make_unique<NbdServer>(volume, socket, spec.bytesPerSecond);
            QString err;
            if (!server->start(&err)) { done(false, err); return; }
            self->m_servers[n] = std::move(server);
            const QString rate = spec.bytesPerSecond > 0
                                     ? QString(", read at %1 MB/s").arg(double(spec.bytesPerSecond) / 1e6, 0, 'f', 1)
                                     : QString();
            self->plug(id, n,
                       QJsonObject{{"driver", "nbd"},
                                   {"server", QJsonObject{{"type", "unix"}, {"path", socket}}},
                                   {"export", NbdServer::kExport},
                                   {"read-only", true}},
                       volume->summary() + rate, done);
        }, Qt::QueuedConnection);
    }).detach();
}

void Sticks::insert(const QString& name, Done done) {
    const QString id = resolve(name);
    if (m_inserted.contains(id) || m_pending.contains(id)) { done(false, id + " is already in"); return; }
    if (m_inserted.size() + m_pending.size() >= kSlots) { done(false, "the deck has two USB slots, both taken"); return; }
    if (!m_machine->running()) { done(false, "the Pi is not running"); return; }

    const int n = m_next++;
    auto plug = [=, this](const QJsonObject& file) { this->plug(id, n, file, QString(), done); };
    if (QFileInfo(id).isDir() || (QFileInfo(id).isFile() && id.endsWith(".stick"))) {
        insertFolder(id, n, done);
        return;
    }

    if (id.startsWith("fd:")) { // an image file, through the same descriptor route as a real stick
        int fd = ::open(QFile::encodeName(id.mid(3)).constData(), O_RDONLY);
        if (fd < 0) { done(false, id + ": cannot open"); return; }
        QString perr;
        bool ok = passFd(fd, 100 + n, &perr);
        ::close(fd);
        if (!ok) { done(false, perr); return; }
        plug(QJsonObject{{"driver", "file"}, {"filename", QString("/dev/fdset/%1").arg(100 + n)}, {"read-only", true}});
        return;
    }
    if (!id.startsWith("disk")) { // an image file
        if (!QFileInfo::exists(id)) { done(false, id + ": no such file"); return; }
        plug(QJsonObject{{"driver", "file"}, {"filename", QFileInfo(id).absoluteFilePath()}, {"read-only", true}});
        return;
    }
    // A real stick: off the Mac's hands first, then a descriptor from authopen.
    run("diskutil", {"unmountDisk", id});
    QString err;
    int fd = openRawDisk(id, &err);
    if (fd < 0) { run("diskutil", {"mountDisk", id}); done(false, err); return; }
    // QEMU opens /dev/fdset/N; the set is filled through add-fd with the
    // descriptor riding along (SCM_RIGHTS) on the QMP socket.
    const int fdset = 100 + n;
    QString perr;
    bool ok = passFd(fd, fdset, &perr);
    ::close(fd); // QEMU holds its own copy now
    if (!ok) {
        run("diskutil", {"mountDisk", id});
        done(false, perr);
        return;
    }
    plug(QJsonObject{{"driver", "host_device"}, {"filename", QString("/dev/fdset/%1").arg(fdset)}, {"read-only", true}});
}

void Sticks::unplug(const QString& name, Done done) {
    const QString id = resolve(name);
    if (!m_inserted.contains(id)) { done(false, id + " is not in"); return; }
    const int n = m_inserted.take(id);
    const QString node = QString("stick%1").arg(n), dev = QString("usbstick%1").arg(n);
    // Pulling a stick: the guest sees it go at once, as dj-usb expects of a yank.
    m_machine->qmp("device_del", QJsonObject{{"id", dev}}, [=, this](const QJsonObject&) {
        QTimer::singleShot(500, this, [=, this] {
            m_machine->qmp("blockdev-del", QJsonObject{{"node-name", node}}, [=, this](const QJsonObject&) {
                dropFdset(100 + n);
                m_servers.erase(n); // QEMU has let go of a folder stick's socket
                if (id.startsWith("disk")) run("diskutil", {"mountDisk", id});
                emit changed();
                done(true, id + " unplugged");
            });
        });
    });
}

void Sticks::forgetAll() {
    for (auto it = m_inserted.begin(); it != m_inserted.end(); ++it)
        if (it.key().startsWith("disk")) run("diskutil", {"mountDisk", it.key()});
    m_inserted.clear();
    m_servers.clear();
    emit changed();
}
