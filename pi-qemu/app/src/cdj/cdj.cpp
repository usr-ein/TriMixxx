#include "cdj/cdj.h"

#include "board/link.h"
#include "board/streamport.h"
#include "util/fail.h"
#include "util/files.h"
#include "util/paths.h"
#include "util/print.h"
#include "util/tool.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>

#include <atomic>
#include <csignal>
#include <memory>
#include <unistd.h>

namespace cdj {

namespace {

QString nxs(const QString& file) { return firmware() + "/nxs/" + file; }

qint64 readPid(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll().trimmed().toLongLong() : 0;
}

QString tail(const QString& path, int lines) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QStringList all = QString::fromUtf8(f.readAll()).split('\n');
    return all.mid(std::max<qsizetype>(0, all.size() - lines)).join('\n').trimmed();
}

// `cdj run`'s child, for the signal handler: SIGINT is nxs_vm's own way to
// stop (its parent watch sends itself one), and it stops QEMU and the GUI
// core with it.
std::atomic<pid_t> g_emulator{0};
void onSignal(int) {
    if (pid_t p = g_emulator.load()) ::kill(p, SIGINT);
}

} // namespace

QString root() {
    const QString env = qEnvironmentVariable("PI_QEMU_CDJ");
    return env.isEmpty() ? QDir::home().filePath(".pi-qemu/cdj") : env;
}

QString firmware() { return root() + "/firmware"; }
QString qemuSource() { return root() + "/qemu-" + QString(kQemuCommit).left(8); }
QString emulator() { return paths::checkout() + "/cdj2000-emulator"; }
QString qemu() { return emulator() + "/build/qemu/build/qemu-system-sh4"; }
QString python() { return emulator() + "/.venv/bin/python"; }

QString macFor(const QString& name) {
    const QByteArray h = QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256);
    return QString::asprintf("02:43:44:%02x:%02x:%02x", uchar(h[0]), 1 + uchar(h[1]) % 254, uchar(h[2]));
}

void requireCheckedOut() {
    if (QFileInfo::exists(emulator() + "/tools/cdj_main/nxs_vm.py")) return;
    if (!paths::isWorktree()) fail("no cdj2000-emulator checked out here: " + tool("cdj build") + " checks it out");
    const bool mainHasIt = QFileInfo::exists(paths::mainCheckout() + "/cdj2000-emulator/.git");
    fail("no cdj2000-emulator in this worktree: " + tool("worktree prepare") + " borrows the main checkout's" +
         (mainHasIt ? QString() : ", which it has not yet: git -C " + paths::mainCheckout() +
                                      " submodule update --init cdj2000-emulator"));
}

void requireBuilt() {
    requireCheckedOut();
    for (const QString& f : {qemu(), emulator() + "/bin/cdj-gui-run", python()})
        if (!QFileInfo::exists(f)) fail("the emulator is not built in this checkout (no " + f + "): " + tool("cdj build"));
}

void requireFirmware() {
    for (const QString& f : {"main-firmware.bin", "gui-boot-memory.elf", "gui-flash-image.bin"})
        if (!QFileInfo::exists(nxs(f)))
            fail("no NXS firmware in " + firmware() + "/nxs: " + tool("cdj firmware ~/Downloads/C2KNXS.UPD"));
}

proc::Result captureTool(const QStringList& args, bool quiet) {
    proc::Options o;
    o.cwd = emulator();
    o.quiet = quiet;
    return proc::capture(python(), QStringList{"-m"} + args, o);
}

int runTool(const QStringList& args) {
    proc::Options o;
    o.cwd = emulator();
    return proc::run(python(), QStringList{"-m"} + args, o);
}

Cdj::Cdj(const QString& name) : m_name(name), m_dir(root() + "/" + name) {
    if (!links::validName(name)) fail("a CDJ's name is letters, digits, - and _: not " + name, 2);
}

QStringList Cdj::all() {
    QStringList out;
    for (const QString& d : QDir(root()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        if (QFileInfo::exists(root() + "/" + d + "/cdj.json")) out << d;
    return out;
}

bool Cdj::exists() const { return QFileInfo::exists(m_dir + "/cdj.json"); }

qint64 Cdj::pid() const {
    const qint64 p = readPid(m_dir + "/pid");
    return p > 0 && proc::name(p) == kTool ? p : 0;
}

QJsonObject Cdj::config() const {
    QFile f(m_dir + "/cdj.json");
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

void Cdj::requireRunning() const {
    if (!exists()) fail("no CDJ " + m_name + ": " + tool("cdj up " + m_name));
    if (!running()) fail(m_name + " is not running: " + tool("cdj up " + m_name));
}

void Cdj::up(const Up& o) {
    if (running()) {
        if (!o.link.isEmpty() || !o.sd.isEmpty() || !o.usb.isEmpty() || o.testTrack || o.window)
            fail(m_name + " is already running: its options cannot change now. " + tool("cdj rm " + m_name) +
                 ", then up again");
        print() << m_name << " is already up: pid " << pid()
                << (link().isEmpty() ? QString() : ", on link " + link()) << "\n";
        return;
    }
    requireBuilt();
    requireFirmware();
    if (!o.link.isEmpty() && !links::validName(o.link)) fail("a link's name is letters, digits, - and _: not " + o.link, 2);
    for (const QString& image : {o.sd, o.usb})
        if (!image.isEmpty() && !QFileInfo(image).isFile()) fail("no image at " + image);
    if (o.testTrack && !o.sd.isEmpty()) fail("--test-track is the CDJ's card: not with --sd", 2);

    QDir().mkpath(m_dir);
    // nxs_vm makes its run directory itself: the last one is kept, once.
    QDir(m_dir + "/run.prev").removeRecursively();
    if (QFileInfo::exists(runDir())) QDir().rename(runDir(), m_dir + "/run.prev");
    QFile::remove(socket());
    QJsonObject c{{"name", m_name},
                  {"link", o.link},
                  {"mac", macFor(m_name)},
                  {"sd", o.sd},
                  {"usb", o.usb},
                  {"test_track", o.testTrack},
                  {"window", o.window},
                  {"checkout", paths::checkout()},
                  {"started", QDateTime::currentDateTime().toString(Qt::ISODate)}};
    files::write(m_dir + "/cdj.json", QJsonDocument(c).toJson());

    QElapsedTimer t;
    t.start();
    const QStringList args{"cdj", "run", m_name};
    files::write(log(), {});
    QProcess p;
    p.setProgram(paths::binary());
    p.setArguments(args);
    p.setStandardInputFile(QProcess::nullDevice());
    p.setStandardOutputFile(log(), QIODevice::Append);
    p.setStandardErrorFile(log(), QIODevice::Append);
    // Its own session: a ^C or a closed terminal here does not reach it.
    p.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
    qint64 started = 0;
    if (!p.startDetached(&started)) fail("cannot start " + paths::binary() + ": " + p.errorString());
    files::write(m_dir + "/pid", QByteArray::number(started) + "\n");
    try {
        waitUp(o.link);
    } catch (const Failure& f) {
        // Not left running, unseen: it would hold two cores for a day.
        stop();
        throw Failure{f.message + "\n" + m_name + " is stopped; its logs stay in " + m_dir + " (" +
                          tool("cdj rm " + m_name) + " deletes them)",
                      f.status};
    }
    print() << m_name << " is up (booted in " << t.elapsed() / 1000 << " s): pid " << started
            << (o.link.isEmpty() ? QString(", no cable in") : ", on link " + o.link) << "\n"
            << "  " << tool("cdj shot " + m_name + " FILE.png") << "\n"
            << "  " << tool("cdj press " + m_name + " KEY") << "   (play, cue, link, usb, sd, enter, back, menu, ...)\n"
            << "  run directory: " << runDir() << "\n"
            << "  when done: " << tool("cdj rm " + m_name) << "\n";
}

// Until its player screen is up (MAIN's first whole browser reply to the GUI:
// "Not Loaded."), then, on a link, until its first keep-alive, which says
// what it claimed.
void Cdj::waitUp(const QString& net) {
    auto alive = [this] {
        if (!running()) fail(QString("%1 cdj run exited (%2):\n%3").arg(kTool, log(), tail(log(), 20)));
    };
    QElapsedTimer t;
    t.start();
    for (;;) {
        alive();
        if (QFileInfo::exists(runDir() + "/session.json")) {
            const proc::Result r = captureTool({"tools.cdj_main.dev", runDir(), "status", "--json"}, true);
            const QJsonObject browser =
                QJsonDocument::fromJson(r.out).object().value("link").toObject().value("browser").toObject();
            if (browser.value("complete").toBool()) break;
        }
        if (t.elapsed() > 120000) fail(m_name + ": no player screen in 120 s (" + log() + ")");
        proc::sleep(1000);
    }
    print() << m_name << ": player screen up in " << t.elapsed() / 1000 << " s\n";
    if (net.isEmpty()) return;
    LinkTap tap(net);
    QString err;
    if (!tap.open(&err)) fail(err);
    const QString mac = macFor(m_name);
    std::vector<char> frame;
    while (t.elapsed() < 180000) {
        alive();
        if (!tap.next(&frame, nullptr, 500)) continue;
        const links::KeepAlive k = links::keepAlive(frame.data(), frame.size());
        if (k.number && k.mac.compare(mac, Qt::CaseInsensitive) == 0) {
            print() << m_name << ": player " << k.number << " at " << k.ip << ", " << k.mac << " on link " << net << "\n";
            return;
        }
    }
    print() << m_name << ": nothing heard from it on link " << net << " in 180 s (" << tool("cdj status " + m_name)
            << ")\n";
}

void Cdj::stop() {
    const qint64 p = pid();
    if (!p) return;
    // nxs_vm's own stop first: it closes QEMU and the GUI core, and then
    // `cdj run` leaves the link. A signal if that does not do.
    captureTool({"tools.cdj_main.dev", runDir(), "stop"}, true);
    for (int i = 0; i < 100 && running(); i++) proc::sleep(200);
    if (running()) ::kill(pid_t(p), SIGTERM);
    for (int i = 0; i < 50 && running(); i++) proc::sleep(200);
    if (running()) ::kill(-pid_t(p), SIGKILL); // its session: the emulator's processes too
}

void Cdj::rm() {
    stop();
    QDir(m_dir).removeRecursively();
    print() << m_name << " removed\n";
}

int Cdj::serve() {
    const QJsonObject c = config();
    if (c.isEmpty()) fail("no CDJ " + m_name + " (" + m_dir + "/cdj.json)");
    const QString net = c.value("link").toString();
    std::unique_ptr<Link> link;
    std::unique_ptr<StreamPort> port;
    QStringList args{"-m", "tools.cdj_main.nxs_vm", runDir(), "--seconds", "86400", "--lightweight",
                     "--qemu", qemu(), "--main-firmware", nxs("main-firmware.bin"), "--gui-firmware", firmware() + "/nxs"};
    if (!net.isEmpty()) {
        QString err;
        link = std::make_unique<Link>(net, m_name);
        if (!link->open(&err)) fail(err);
        port = std::make_unique<StreamPort>(socket(), link->qemuFd());
        if (!port->open(&err)) fail(err);
        args << "--link-hub" << "unix:" + socket() << "--link-mac" << c.value("mac").toString();
    }
    if (!c.value("sd").toString().isEmpty()) args << "--sd" << c.value("sd").toString();
    if (!c.value("usb").toString().isEmpty()) args << "--usb" << c.value("usb").toString();
    if (c.value("test_track").toBool()) args << "--test-track";
    if (c.value("window").toBool()) args << "--ui";

    QProcess emu;
    emu.setProgram(python());
    emu.setArguments(args);
    emu.setWorkingDirectory(emulator());
    emu.setProcessChannelMode(QProcess::ForwardedChannels);
    emu.setStandardInputFile(QProcess::nullDevice());
    for (int sig : {SIGTERM, SIGINT, SIGHUP}) std::signal(sig, onSignal);
    print() << "cdj " << m_name << ": " << proc::join(QStringList{python()} + args) << "\n";
    emu.start();
    if (!emu.waitForStarted(10000)) fail("cannot start " + python() + ": " + emu.errorString());
    g_emulator = pid_t(emu.processId());
    emu.waitForFinished(-1);
    g_emulator = 0;
    print() << "cdj " << m_name << ": the emulator exited (" << emu.exitCode() << ")"
            << (net.isEmpty() ? QString() : ", leaving link " + net) << "\n";
    port.reset();
    link.reset();
    return emu.exitStatus() == QProcess::NormalExit ? emu.exitCode() : 1;
}

int Cdj::dev(const QStringList& args) const {
    if (!exists()) fail("no CDJ " + m_name + ": " + tool("cdj up " + m_name));
    return runTool(QStringList{"tools.cdj_main.dev", runDir()} + args);
}

} // namespace cdj
