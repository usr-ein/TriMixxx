// pi-qemu run: one emulated board, what `deck up` starts for each instance;
// and pi-qemu build-log: a build's window.

#include "cli/cli.h"
#include "cli/commands/commands.h"

#include "board/controlserver.h"
#include "board/link.h"
#include "board/machine.h"
#include "board/sticks.h"
#include "s3/virtuals3.h"
#include "s3/wiring.h"
#include "ui/buildwindow.h"
#include "ui/deckwindow.h"
#include "util/fail.h"
#include "util/paths.h"
#include "util/tool.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QSocketNotifier>
#include <QTcpServer>
#include <QTextStream>
#include <QTimer>

#include <csignal>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>

namespace {

// QEMU is pi-qemu's child: a pi-qemu that is killed must not leave it running
// (holding the card open and its ports bound). SIGTERM/SIGINT/SIGHUP quit the
// event loop, so the Machine stops QEMU on the way out; the handler also
// signals QEMU itself, in case the loop never gets there.
int g_sigFd[2] = {-1, -1};
void onSignal(int sig) {
    if (pid_t p = g_qemuPid.load()) ::kill(p, SIGTERM);
    char c = char(sig);
    (void)::write(g_sigFd[1], &c, 1);
}

int run(cli::Args& a) {
    const QString card = QFileInfo(a.take("CARD")).absoluteFilePath();
    a.done();
    const bool windowed = !a.has("no-controls");
    if (windowed) QApplication::setQuitOnLastWindowClosed(true);

    ::socketpair(AF_UNIX, SOCK_STREAM, 0, g_sigFd);
    QSocketNotifier sigNotifier(g_sigFd[0], QSocketNotifier::Read);
    QObject::connect(&sigNotifier, &QSocketNotifier::activated, [] {
        char c;
        (void)::read(g_sigFd[0], &c, 1);
        QCoreApplication::quit();
    });
    for (int sig : {SIGTERM, SIGINT, SIGHUP}) std::signal(sig, onSignal);

    MachineOptions o;
    o.card = card;
    o.runDir = QFileInfo(card).absolutePath() + "/" + QFileInfo(card).completeBaseName() + ".run";
    QDir().mkpath(o.runDir);
    const QString tools = a.value("tools", paths::tools());
    o.qemu = tools + "/qemu-system-aarch64";
    o.dtmerge = tools + "/dtmerge";
    o.display = a.value("display", "window");
    o.audio = a.value("audio", "none");
    o.mac = a.value("mac", o.mac);
    o.net = a.value("net", o.net);
    o.accel = a.value("accel", o.accel);
    o.serialLog = a.value("serial-log");
    o.sticks = a.values("stick");
    if (a.has("restore")) o.restore = QFileInfo(a.value("restore")).absoluteFilePath();
    if (a.has("ssh")) {
        o.sshPort = a.number("ssh", 0);
    } else {
        // Any free port, taken from the kernel and let go just before QEMU binds it.
        QTcpServer probe;
        if (!probe.listen(QHostAddress::LocalHost, 0)) fail("no free port for ssh");
        o.sshPort = probe.serverPort();
    }
    {
        QFile portFile(o.runDir + "/ssh.port");
        if (portFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) portFile.write(QByteArray::number(o.sshPort) + "\n");
    }
    const QStringList g = a.value("panel", "1280x800x16").split('x');
    if (g.size() != 3) cli::usage("--panel WIDTHxHEIGHTxDEPTH");
    o.fbWidth = g[0].toInt(), o.fbHeight = g[1].toInt(), o.fbDepth = g[2].toInt();
    for (const QString& f : {o.qemu, o.dtmerge})
        if (!QFileInfo::exists(f)) fail(f + " missing: run pi-qemu/build.sh");

    QTextStream out(stdout);
    Wiring wiring;
    QString err;
    if (!wiring.load(paths::units(), a.value("deck", "trimixxx0"), &err)) fail(err);
    // Before the Machine, so it outlives every QEMU that holds its end.
    o.name = a.value("name");
    std::unique_ptr<Link> link;
    if (a.has("link")) {
        link = std::make_unique<Link>(a.value("link"), o.name.isEmpty() ? QFileInfo(card).completeBaseName() : o.name);
        if (!link->open(&err)) fail(err);
        link->setPlugged(!a.has("unplugged"));
        o.link = link.get();
    }
    Machine machine(o);
    VirtualS3 s3(machine.s3Socket());
    Sticks sticks(&machine, a.value("sticks", paths::cache() + "/sticks"));
    ControlServer control(&machine, &s3, &sticks, &wiring);
    // Test hook: PI_QEMU_SNAPSHOT=file.png renders the control panel to a file
    // and quits, with no Pi started and no control socket taken.
    const QString shot = qEnvironmentVariable("PI_QEMU_SNAPSHOT");
    if (shot.isEmpty() && !control.listen(o.runDir + "/control.sock", &err)) fail(err);

    QObject::connect(&machine, &Machine::status, [&out](const QString& s) { out << kTool << ": " << s << Qt::endl; });
    QObject::connect(&machine, &Machine::poweredOff, [&] { sticks.forgetAll(); });
    auto powerOn = [&]() -> bool {
        if (machine.running()) return true;
        QString e;
        if (machine.powerOn(&e)) return true;
        out << kTool << ": " << e << Qt::endl;
        return false;
    };
    QObject::connect(&control, &ControlServer::powerOnRequested, powerOn);

    std::unique_ptr<DeckWindow> window;
    if (windowed) {
        window = std::make_unique<DeckWindow>(&machine, &s3, &sticks, &wiring);
        QObject::connect(window.get(), &DeckWindow::powerOnRequested, powerOn);
        window->show();
        if (!shot.isEmpty()) {
            QTimer::singleShot(1000, window.get(), [&window, shot] { window->grab().save(shot); QCoreApplication::quit(); });
            return QCoreApplication::exec();
        }
    } else {
        // Headless, the board is all there is: it ends with the Pi.
        QObject::connect(&machine, &Machine::poweredOff, QCoreApplication::instance(), &QCoreApplication::quit);
    }
    out << kTool << ": card " << o.card << "\n         run dir " << o.runDir
        << "\n         sound " << (o.audio == "none" ? "OFF" : o.audio)
        << "\n         eth0 " << (link ? QString("on link %1 as %2%3").arg(link->net(), link->member(),
                                                                         link->plugged() ? "" : " (unplugged)")
                                         : QString("on no link"))
        << "\n         ssh -p " << o.sshPort << " sam1902@127.0.0.1"
        << "\n         control socket " << o.runDir << "/control.sock" << Qt::endl;
    // A board that cannot start says so and, headless, exits: whoever started
    // it is not left waiting for an ssh that will never answer.
    if (!powerOn() && !windowed) return 1;
    return QCoreApplication::exec();
}

int buildLog(cli::Args& a) {
    const QString log = a.take("LOG");
    a.done();
    BuildWindow w(log);
    w.show();
    w.raise();
    // Test hook: PI_QEMU_SNAPSHOT=file.png renders the window to a file after
    // a moment and quits (QT_QPA_PLATFORM=offscreen needs no screen).
    if (QString shot = qEnvironmentVariable("PI_QEMU_SNAPSHOT"); !shot.isEmpty())
        QTimer::singleShot(2500, &w, [&w, shot] { w.grab().save(shot); QCoreApplication::quit(); });
    return QCoreApplication::exec();
}

} // namespace

namespace commands {

void addRun(cli::Registry& r) {
    r.add({
        .name = "run",
        .synopsis = "CARD [options]",
        .summary = "one emulated board: what `deck up` starts",
        .help = "CARD is an SD card image, raw, its size a power of two. The Pi's screen is QEMU's\n"
                "window (--display), its controls this one's (--no-controls: none). Sound is OFF\n"
                "unless --audio says otherwise; never --audio speakers unless the person at the\n"
                "laptop asked for sound. The board's control socket, <card>.run/control.sock, is\n"
                "what `deck` verbs drive it through. Headless, it exits with the Pi.",
        .options = {
            {"deck", "NAME", "whose wiring the controls follow (mixxx_config/units/NAME.json); trimixxx0"},
            {"display", "MODE", "window | vnc | none; window"},
            {"no-controls", {}, "no control panel: driven through the control socket only"},
            {"audio", "MODE", "none | speakers | wav:FILE; none, which is silent"},
            {"ssh", "PORT", "the Pi's ssh on 127.0.0.1:PORT; default a free port, in <run dir>/ssh.port"},
            {"net", "MODE", "user | restricted | none; user (the management NIC: ssh, apt)"},
            {"link", "NET", "eth0 on the link NET, the network every board started with the\n"
                            "same NET shares (~/.pi-qemu/links/NET/); else on an empty switch"},
            {"unplugged", {}, "with --link: frames go nowhere until the control socket's `link plug`;\n"
                              "the Pi still sees a link (what a restored deck waits in)"},
            {"name", "NAME", "the board's name: on its link (default the card's file name)\n"
                             "and in its windows' titles"},
            {"mac", "MAC", "eth0's MAC, written into the device tree"},
            {"panel", "WxHxD", "the framebuffer, as the deck's panel; 1280x800x16"},
            {"accel", "ACCEL", "hvf (fast) | tcg (a real Cortex-A72 model, slow); hvf"},
            {"restore", "FILE", "start from a saved machine (deck save) rather than a boot;\n"
                                "CARD must be the card it was saved with"},
            {"stick", "FILE", "an image plugged in as a USB stick at power-on (repeatable)"},
            {"sticks", "DIR", "USB stick images (*.img) offered alongside the Mac's own"},
            {"serial-log", "FILE", "the S3's UART into a file instead (debugging)"},
            {"tools", "DIR", "qemu-system-aarch64 and dtmerge; the main checkout's qemu/.build/bin"},
        },
        .window = [](const cli::Args& a) { return !a.has("no-controls") && a.positional().size() == 1; },
        .run = run,
    });
    r.add({
        .name = "build-log",
        .synopsis = "LOG",
        .summary = "a build's window: its steps, progress and log",
        .window = [](const cli::Args& a) { return a.positional().size() == 1; },
        .run = buildLog,
    });
}

} // namespace commands
