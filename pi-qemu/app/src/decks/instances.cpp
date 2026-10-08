#include "decks/instances.h"

#include "decks/controlclient.h"
#include "decks/readiness.h"
#include "util/fail.h"
#include "util/files.h"
#include "util/paths.h"
#include "util/process.h"
#include "util/tool.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTextStream>

#include <csignal>

namespace {

QTextStream& out() {
    static QTextStream s(stdout);
    return s;
}

qint64 readPid(const QString& file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return 0;
    return f.readAll().trimmed().toLongLong();
}

// QEMU's, from <run dir>/qemu.pid: stopped outright, if it is still QEMU.
void killQemu(const QString& runDir) {
    const qint64 q = readPid(runDir + "/qemu.pid");
    if (q > 0 && proc::name(q).startsWith("qemu-system")) {
        ::kill(pid_t(q), SIGKILL);
        for (int i = 0; i < 25 && proc::name(q).startsWith("qemu-system"); i++) proc::sleep(200);
    }
    QFile::remove(runDir + "/qemu.pid");
}

QString tail(const QString& file, int lines) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QStringList all = QString::fromUtf8(f.readAll()).split('\n');
    return all.mid(qMax(0, all.size() - lines)).join('\n').trimmed();
}

} // namespace

Instance::Instance(const QString& name) : m_name(name) {
    static const QRegularExpression ok("^[a-zA-Z0-9][a-zA-Z0-9_-]*$");
    if (!ok.match(name).hasMatch()) fail("an instance's NAME is letters, digits, - and _: not " + name, 2);
    m_dir = paths::instances() + "/" + name;
}

QStringList Instance::all() {
    return QDir(paths::instances()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)
        .filter(QRegularExpression("^[^.]"));
}

bool Instance::exists() const { return QFileInfo::exists(m_dir); }

qint64 Instance::pid() const {
    const qint64 p = readPid(m_dir + "/pid");
    return p > 0 && proc::name(p) == kTool ? p : 0;
}

int Instance::sshPort() const { return int(readPid(runDir() + "/ssh.port")); }

void Instance::requireRunning() const {
    if (!exists()) fail("no instance " + m_name + ": " + tool("deck up " + m_name));
    if (!running()) fail(m_name + " is not running: " + tool("deck up " + m_name));
}

void Instance::checkAlive() const {
    if (!running())
        fail(QString("%1 run exited (%2, %3/qemu.log):\n%4")
                 .arg(kTool, log(), runDir(), tail(log(), 15)));
    // A saved machine QEMU would not load never boots: no ssh to wait for.
    QFile f(log());
    if (f.open(QIODevice::ReadOnly) && f.readAll().contains("the saved machine did not load"))
        fail(m_name + ": its saved machine did not load (" + runDir() + "/qemu.log). " +
             tool("deck rm " + m_name) + ", then up again; or --boot");
}

void Instance::writeSshConfig(int port) {
    // The aliases NAME and deck, for this instance only: ~/.ssh/config is not touched.
    QFile f(m_dir + "/ssh_config");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) fail("cannot write " + f.fileName());
    f.write(QString("Host %1 deck\n"
                    "  HostName 127.0.0.1\n"
                    "  Port %2\n"
                    "  User sam1902\n"
                    "  IdentityFile %3\n"
                    "  IdentitiesOnly yes\n"
                    "  StrictHostKeyChecking no\n"
                    "  UserKnownHostsFile /dev/null\n"
                    "  LogLevel ERROR\n"
                    "  ConnectTimeout 10\n"
                    "  ServerAliveInterval 15\n"
                    "  SetEnv LC_ALL=C.UTF-8\n").arg(m_name).arg(port).arg(paths::sshKey()).toUtf8());
    f.close();
    ssh().writeWrappers(binDir());
}

void Instance::launch(const QStringList& runArgs) {
    QDir().mkpath(runDir());
    QFile::remove(runDir() + "/ssh.port");
    const QStringList args = QStringList{"run", "--deck", "trimixxx0"} + runArgs + QStringList{card()};
    files::write(m_dir + "/command", (proc::join(QStringList{paths::binary()} + args) + "\n").toUtf8());
    files::write(log(), {});
    QProcess p;
    p.setProgram(paths::binary());
    p.setArguments(args);
    p.setStandardInputFile(QProcess::nullDevice());
    p.setStandardOutputFile(log(), QIODevice::Append);
    p.setStandardErrorFile(log(), QIODevice::Append);
    // Its own session: a ^C or a closed terminal here does not reach it.
    p.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
    qint64 pid = 0;
    if (!p.startDetached(&pid)) fail("cannot start " + paths::binary() + ": " + p.errorString());
    files::write(m_dir + "/pid", QByteArray::number(pid) + "\n");
    for (int i = 0; i < 50 && sshPort() == 0; i++) {
        checkAlive();
        proc::sleep(200);
    }
    if (sshPort() == 0) fail(QString(kTool) + " run did not start: " + log());
    writeSshConfig(sshPort());
}

void Instance::up(const Up& o) {
    if (running()) {
        // Reused as it is: one start serves every later call, but not
        // silently with other settings than asked for.
        if (o.window || o.fresh || o.boot || !o.from.isEmpty() || !o.runArgs.isEmpty())
            fail(m_name + " is already running: its options cannot change now. " + tool("deck rm " + m_name) +
                 " (or deck stop " + m_name + "), then up again");
        out() << m_name << " is already up: ssh port " << sshPort() << ", pid " << pid() << "\n"
              << "  " << tool("deck ssh " + m_name) << "\n"
              << "  when done: " << tool("deck rm " + m_name) << "   (or deck stop " << m_name << " to keep it, suspended)\n";
        return;
    }
    if (!QFileInfo::exists(paths::tools() + "/qemu-system-aarch64"))
        fail("no QEMU in " + paths::tools() + ": run pi-qemu/build.sh");
    requireUsableKey(paths::sshKey());
    QDir().mkpath(m_dir);
    const QString golden = paths::golden();
    if (!o.from.isEmpty()) { // a card of its own: booted, no saved machine
        if (!QFileInfo::exists(o.from)) fail("no card at " + o.from);
        QFile::remove(state());
        files::clone(o.from, card());
        // QEMU's SD card is a power of two; a real card is any size. The
        // clone is rounded up, and stays sparse.
        const qint64 size = QFileInfo(card()).size();
        qint64 p2 = 1;
        while (p2 < size) p2 *= 2;
        if (p2 != size && !QFile::resize(card(), p2)) fail("cannot grow " + card() + " to a power of two");
    } else if (o.fresh || !QFileInfo::exists(card())) {
        if (!QFileInfo::exists(golden + ".img")) fail("no golden card (" + golden + ".img): " + tool("deck golden [CARD]"));
        QFile::remove(state());
        files::clone(golden + ".img", card());
        if (QFileInfo::exists(golden + ".state")) files::clone(golden + ".state", state());
    }

    QStringList args;
    if (!o.window) args << "--no-controls" << "--display" << "none";
    // The saved machine leaves `state` before pi-qemu starts, and is deleted
    // once it is up or has failed: it belongs to the card as it was, and the
    // card moves on as soon as the machine runs.
    QString restoring;
    if (o.boot) {
        QFile::remove(state());
    } else if (QFileInfo::exists(state())) {
        QDir().mkpath(runDir());
        restoring = runDir() + "/restoring.state";
        QFile::remove(restoring);
        if (!QFile::rename(state(), restoring)) fail("cannot move " + state());
        args << "--restore" << restoring;
    }
    auto forget = qScopeGuard([&] { if (!restoring.isEmpty()) QFile::remove(restoring); });
    args << o.runArgs;

    QElapsedTimer t;
    t.start();
    launch(args);
    readiness::waitSsh(ssh(), 300, [this] { checkAlive(); });
    if (!restoring.isEmpty()) {
        // The machine wakes with the clock it was saved with: give it now.
        ssh().capture(QString("sudo date -u -s @%1 >/dev/null").arg(QDateTime::currentSecsSinceEpoch()));
    }
    out() << m_name << " is up (" << (restoring.isEmpty() ? "booted" : "restored") << " in " << t.elapsed() / 1000
          << " s): ssh port " << sshPort() << ", pid " << pid() << (o.window ? ", with windows" : "") << "\n"
          << "  " << tool("deck ssh " + m_name) << "\n"
          << "  " << tool("deck status " + m_name) << "\n"
          << "  console log: " << runDir() << "/console.log\n"
          << "  when done: " << tool("deck rm " + m_name) << "   (or deck stop " << m_name << " to keep it, suspended)\n";
}

void Instance::pullPlug() {
    if (const qint64 p = pid()) {
        ::kill(pid_t(p), SIGTERM); // pi-qemu stops its QEMU on the way out
        for (int i = 0; i < 50 && running(); i++) proc::sleep(200);
        if (running()) ::kill(pid_t(p), SIGKILL);
        for (int i = 0; i < 25 && running(); i++) proc::sleep(200);
    }
    // A pi-qemu killed outright leaves its QEMU behind, still holding the
    // card and the ports.
    killQemu(runDir());
}

void Instance::suspend() {
    const QString next = state() + ".new";
    QFile::remove(next);
    const control::Reply r = control::send(controlSocket(), "save " + next, 600000);
    if (!r.ok) {
        QFile::remove(next);
        fail("could not save " + m_name + ": " + r.text);
    }
    QFile::remove(state());
    if (!QFile::rename(next, state())) fail("cannot move " + next);
    // Headless, pi-qemu exits with the Pi; with windows it stays open.
    for (int i = 0; i < 50 && running(); i++) proc::sleep(200);
    pullPlug();
}

void Instance::stop() {
    if (!running()) { out() << m_name << " is not running\n"; return; }
    suspend();
    out() << m_name << " suspended: " << tool("deck up " + m_name) << " resumes it\n";
}

void Instance::down() {
    if (!running()) { out() << m_name << " is not running\n"; return; }
    QFile::remove(state());
    proc::Options o;
    o.quiet = true;
    o.timeoutMs = 20000;
    ssh().capture("sudo systemctl poweroff", o);
    for (int i = 0; i < 45 && running(); i++) {
        // With windows, pi-qemu outlives the Pi: close it once QEMU is gone.
        if (!QFileInfo::exists(runDir() + "/qemu.pid")) ::kill(pid_t(pid()), SIGTERM);
        proc::sleep(2000);
    }
    if (running()) {
        QTextStream(stderr) << m_name << " did not power off: pulling the plug\n";
        pullPlug();
    }
    out() << m_name << " is off\n";
}

void Instance::kill() {
    QFile::remove(state());
    pullPlug();
    out() << m_name << ": plug pulled\n";
}

void Instance::remove() {
    pullPlug();
    if (!QDir(m_dir).removeRecursively()) fail("cannot delete " + m_dir);
    out() << m_name << " removed\n";
}

void Instance::golden(QString card) {
    if (card.isEmpty()) card = paths::cache() + "/build/trimixxx0.img";
    card = QFileInfo(card).absoluteFilePath();
    if (!QFileInfo::exists(card)) fail("no card at " + card);
    if (files::inUse(card)) fail(card + " is in use (a running deck?): power it off first");
    requireUsableKey(paths::sshKey());
    Instance g("golden-make", paths::instances() + "/.golden");
    g.pullPlug();
    QDir(g.m_dir).removeRecursively();
    QDir().mkpath(g.m_dir);
    files::clone(card, g.card());
    out() << "booting " << card << " (headless, silent) to save it..." << Qt::endl;
    g.launch({"--no-controls", "--display", "none"});
    readiness::waitSsh(g.ssh(), 300, [&g] { g.checkAlive(); });
    // Saved once Mixxx has its sound device open and has settled: that is the
    // deck every instance wakes up as.
    QElapsedTimer t;
    t.start();
    proc::Options o;
    o.quiet = true;
    while (!g.ssh().capture("grep -q 'Started stream successfully' /tmp/mixxx/mixxx.log", o).ok()) {
        g.checkAlive();
        if (t.elapsed() > 300000) fail("Mixxx did not open its sound device: deck golden on " + card);
        proc::sleep(2000);
    }
    proc::sleep(10000);
    g.suspend();
    const QString golden = paths::golden();
    QDir().mkpath(QFileInfo(golden).absolutePath());
    for (const auto& [from, to] : {std::pair{g.state(), golden + ".state"}, std::pair{g.card(), golden + ".img"}}) {
        QFile::remove(to + ".new");
        if (!QFile::rename(from, to + ".new")) fail("cannot move " + from);
    }
    for (const QString& to : {golden + ".state", golden + ".img"}) {
        QFile::remove(to);
        if (!QFile::rename(to + ".new", to)) fail("cannot move " + to + ".new");
    }
    QDir(g.m_dir).removeRecursively();
    out() << "golden: " << golden << ".img + .state ("
          << QFileInfo(golden + ".state").size() / (1024 * 1024) << " MB of machine)\n";
}
