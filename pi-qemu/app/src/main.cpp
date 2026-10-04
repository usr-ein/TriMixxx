// pi-qemu: a TriMixxx deck on an emulated Raspberry Pi 4.
//
//   pi-qemu run [options] CARD.img   the board, the Pi's screen in a window,
//                                    and the deck's controls in another
//   pi-qemu COMMAND ...              drive the running deck from a terminal
//                                    (pi-qemu help)
//
// Sound is OFF unless --audio speakers is given.

#include "control.h"
#include "machine.h"
#include "s3.h"
#include "sticks.h"
#include "wiring.h"
#include "deckwindow.h"
#include "buildwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>

#include <QSocketNotifier>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <sys/socket.h>
#include <unistd.h>

// QEMU is pi-qemu's child: a pi-qemu that is killed must not leave it running
// (holding the card open and its ports bound). SIGTERM/SIGINT/SIGHUP quit the
// event loop, so the Machine stops QEMU on the way out; the handler also
// signals QEMU itself, in case the loop never gets there.
std::atomic<pid_t> g_qemuPid{0};
static int g_sigFd[2] = {-1, -1};
static void onSignal(int sig) {
    if (pid_t p = g_qemuPid.load()) ::kill(p, SIGTERM);
    char c = char(sig);
    (void)::write(g_sigFd[1], &c, 1);
}

namespace {

QString repoDir() { return QDir::cleanPath(PI_QEMU_SOURCE_DIR); }

int run(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("pi-qemu");
    ::socketpair(AF_UNIX, SOCK_STREAM, 0, g_sigFd);
    QSocketNotifier sigNotifier(g_sigFd[0], QSocketNotifier::Read);
    QObject::connect(&sigNotifier, &QSocketNotifier::activated, &app, [] {
        char c;
        (void)::read(g_sigFd[0], &c, 1);
        QCoreApplication::quit();
    });
    for (int sig : {SIGTERM, SIGINT, SIGHUP}) std::signal(sig, onSignal);
    QCommandLineParser p;
    p.setApplicationDescription("Run a TriMixxx deck's SD card on an emulated Raspberry Pi 4.");
    p.addHelpOption();
    p.addPositionalArgument("run", "");
    p.addPositionalArgument("card", "the SD card image (raw, power-of-two size)");
    QCommandLineOption deck("deck", "whose wiring the controls follow (mixxx_config/units/<deck>.json)", "name", "trimixxx0");
    QCommandLineOption display("display", "window | vnc | none", "mode", "window");
    QCommandLineOption audio("audio", "none | speakers | wav:FILE  (none is silent)", "mode", "none");
    QCommandLineOption mac("mac", "eth0's MAC, written into the device tree", "mac", "02:54:4d:58:00:00");
    QCommandLineOption panel("panel", "framebuffer WIDTHxHEIGHTxDEPTH, the deck's panel", "geometry", "1280x800x16");
    QCommandLineOption ssh("ssh", "host port for the Pi's ssh, on 127.0.0.1", "port", "2222");
    QCommandLineOption net("net", "user | restricted | none", "mode", "user");
    QCommandLineOption accel("accel", "hvf (fast) | tcg (a real Cortex-A72 model, slow)", "accel", "hvf");
    QCommandLineOption serialLog("serial-log", "the S3's UART to this file instead (debugging)", "file");
    QCommandLineOption stick("stick", "an image file plugged in as a USB stick at power-on", "file");
    QCommandLineOption sticksDir("sticks", "folder of USB stick images (*.img) to offer", "dir",
                                 repoDir() + "/.cache/sticks");
    QCommandLineOption noControls("no-controls", "no controls window: the command line only");
    QCommandLineOption tools("tools", "directory with qemu-system-aarch64 and dtmerge", "dir",
                             repoDir() + "/qemu/.build/bin");
    p.addOptions({deck, display, audio, mac, panel, ssh, net, accel, serialLog, stick, sticksDir, noControls, tools});
    p.process(app);
    if (p.positionalArguments().size() != 2) p.showHelp(2);

    MachineOptions o;
    o.card = QFileInfo(p.positionalArguments()[1]).absoluteFilePath();
    o.runDir = QFileInfo(o.card).absolutePath() + "/" + QFileInfo(o.card).completeBaseName() + ".run";
    QDir().mkpath(o.runDir);
    o.qemu = p.value(tools) + "/qemu-system-aarch64";
    o.dtmerge = p.value(tools) + "/dtmerge";
    o.display = p.value(display);
    o.audio = p.value(audio);
    o.mac = p.value(mac);
    o.net = p.value(net);
    o.accel = p.value(accel);
    o.serialLog = p.value(serialLog);
    o.sticks = p.values(stick);
    o.sshPort = p.value(ssh).toInt();
    QStringList g = p.value(panel).split('x');
    if (g.size() != 3) { fprintf(stderr, "pi-qemu: --panel WIDTHxHEIGHTxDEPTH\n"); return 2; }
    o.fbWidth = g[0].toInt(); o.fbHeight = g[1].toInt(); o.fbDepth = g[2].toInt();
    for (const QString& f : {o.qemu, o.dtmerge})
        if (!QFileInfo::exists(f)) { fprintf(stderr, "pi-qemu: %s missing: run qemu/build.sh\n", qPrintable(f)); return 1; }

    QTextStream out(stdout);
    Wiring wiring;
    QString err;
    if (!wiring.load(QDir(repoDir()).filePath("../mixxx_config/units"), p.value(deck), &err)) {
        fprintf(stderr, "pi-qemu: %s\n", qPrintable(err));
        return 1;
    }
    Machine machine(o);
    S3 s3(machine.s3Socket());
    Sticks sticks(&machine, p.value(sticksDir));
    Control control(&machine, &s3, &sticks, &wiring);
    // Test hook: PI_QEMU_SNAPSHOT=file.png renders the control panel to a file
    // and quits, with no Pi started and no control socket taken from a running one.
    const QString shot = qEnvironmentVariable("PI_QEMU_SNAPSHOT");
    if (shot.isEmpty() && !control.listen(o.runDir + "/control.sock", &err)) { fprintf(stderr, "pi-qemu: %s\n", qPrintable(err)); return 1; }

    QObject::connect(&machine, &Machine::status, [&out](const QString& s) { out << "pi-qemu: " << s << Qt::endl; });
    QObject::connect(&machine, &Machine::poweredOff, [&] { sticks.forgetAll(); });
    auto powerOn = [&] {
        if (machine.running()) return;
        QString e;
        if (!machine.powerOn(&e)) out << "pi-qemu: " << e << Qt::endl;
    };
    QObject::connect(&control, &Control::powerOnRequested, powerOn);

    std::unique_ptr<DeckWindow> window;
    if (!p.isSet(noControls)) {
        window = std::make_unique<DeckWindow>(&machine, &s3, &sticks, &wiring);
        QObject::connect(window.get(), &DeckWindow::powerOnRequested, powerOn);
        window->show();
        if (!shot.isEmpty()) {
            QTimer::singleShot(1000, window.get(), [&window, shot] { window->grab().save(shot); QCoreApplication::quit(); });
            return app.exec();
        }
    } else {
        QApplication::setQuitOnLastWindowClosed(false);
        QObject::connect(&machine, &Machine::poweredOff, &app, &QApplication::quit);
    }
    out << "pi-qemu: card " << o.card << "\n         run dir " << o.runDir
        << "\n         sound " << (o.audio == "none" ? "OFF" : o.audio)
        << "\n         ssh -p " << o.sshPort << " sam1902@127.0.0.1"
        << "\n         drive it: pi-qemu help" << Qt::endl;
    powerOn();
    return app.exec();
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && QString(argv[1]) == "run") return run(argc, argv);
    if (argc == 3 && QString(argv[1]) == "build-log") { // a window on image/build.sh's log
        QApplication app(argc, argv);
        BuildWindow w(QString::fromLocal8Bit(argv[2]));
        w.show();
        w.raise();
        // Test hook: PI_QEMU_SNAPSHOT=file.png renders the window to a file
        // after a moment and quits (QT_QPA_PLATFORM=offscreen needs no screen).
        if (QString shot = qEnvironmentVariable("PI_QEMU_SNAPSHOT"); !shot.isEmpty())
            QTimer::singleShot(2500, &w, [&w, shot] { w.grab().save(shot); QCoreApplication::quit(); });
        return app.exec();
    }
    QCoreApplication app(argc, argv);
    QStringList words = app.arguments().mid(1);
    if (words.isEmpty() || words[0] == "--help" || words[0] == "-h" || words[0] == "help") {
        printf("usage: pi-qemu run [options] CARD.img   (pi-qemu run --help)\n"
               "       pi-qemu build-log LOGFILE        watch an image build\n"
               "       pi-qemu COMMAND ...              drive the running deck\n\n%s\n",
               qPrintable(Control::help()));
        return words.isEmpty() ? 2 : 0;
    }
    return runClientCommand(currentControlSocket(), words);
}
