// pi-qemu cdj: emulated CDJ-2000NXSs, Pioneer's own firmware on
// cdj2000-emulator (cdj/cdj.h, docs/cdj-emulator.md), on the decks' links.

#include "cli/cli.h"
#include "cli/commands/commands.h"

#include "cdj/cdj.h"
#include "util/fail.h"
#include "util/files.h"
#include "util/paths.h"
#include "util/print.h"
#include "util/process.h"
#include "util/tool.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

// The NXS updater (C2KNXS.UPD), split into its components, which the
// emulator's extractors check (CRCs, checksums) and turn into what it boots.
// Into a new directory, then in place of the old one: a CDJ never boots half
// an extraction.
int firmwareVerb(cli::Args& a) {
    const QString upd = QFileInfo(a.take("C2KNXS.UPD")).absoluteFilePath();
    a.done();
    cdj::requireBuilt();
    if (!QFileInfo(upd).isFile()) fail("no file " + upd);
    for (const QString& n : cdj::Cdj::all())
        if (cdj::Cdj(n).running()) fail(n + " is running on this firmware: " + tool("cdj rm " + n) + " first");
    const QString fresh = cdj::root() + "/firmware.new", fw = cdj::firmware();
    QDir(fresh).removeRecursively();
    if (!QDir().mkpath(fresh + "/nxs/updates")) fail("cannot make " + fresh);
    for (const QStringList& step : {QStringList{"tools.cdj_gui.nxs_container", upd, fresh + "/nxs/updates"},
                                    QStringList{"tools.cdj_gui.extract", fresh + "/nxs/updates/C2KGUI.UPD", fresh + "/nxs"},
                                    QStringList{"tools.cdj_gui.main_unpack", fresh + "/nxs/updates/C2KMAIN.UPD", fresh + "/nxs"}})
        if (cdj::runTool(step) != 0) {
            QDir(fresh).removeRecursively();
            fail("the emulator's " + step[0] + " refused " + upd);
        }
    QDir(fw).removeRecursively();
    if (!QDir().rename(fresh, fw)) fail("cannot move " + fresh + " to " + fw);
    print() << "NXS firmware in " << fw << "/nxs, outside every checkout\n";
    return 0;
}

// The checkout's emulator: QEMU with the CDJ boards, at the pinned revision
// (fetched once, then an APFS clone per checkout), the fast GUI core, and a
// venv for the emulator's Python tools.
int build(cli::Args& a) {
    a.done();
    const QString emu = cdj::emulator();
    if (!QFileInfo::exists(emu + "/tools/cdj_main/nxs_vm.py")) {
        // Only the main checkout makes a clone of its own. A worktree borrows
        // the main checkout's (worktree prepare): a private clone would hold
        // the worktree until `worktree release` takes it back.
        if (paths::isWorktree()) cdj::requireCheckedOut();
        if (proc::run("git", {"-C", paths::checkout(), "submodule", "update", "--init", "cdj2000-emulator"}) != 0)
            fail("could not check out cdj2000-emulator");
    }
    const QString src = cdj::qemuSource();
    if (!QFileInfo::exists(src + "/VERSION")) {
        const QString fetching = src + ".fetching";
        QDir(fetching).removeRecursively();
        QDir().mkpath(fetching);
        print() << "fetching QEMU " << QString(cdj::kQemuCommit).left(8) << " into " << src << " (once)\n";
        for (const QStringList& g : {QStringList{"init", "--quiet"},
                                     QStringList{"fetch", "--quiet", "--depth", "1", cdj::kQemuUrl, cdj::kQemuCommit},
                                     QStringList{"-c", "advice.detachedHead=false", "checkout", "--quiet", "FETCH_HEAD"}})
            if (proc::run("git", QStringList{"-C", fetching} + g) != 0) fail("could not fetch QEMU " + QString(cdj::kQemuCommit));
        if (!QDir().rename(fetching, src)) fail("cannot move " + fetching + " to " + src);
    }
    const QString tree = emu + "/build/qemu";
    if (!QFileInfo::exists(tree + "/VERSION")) {
        QDir().mkpath(emu + "/build");
        if (proc::run("cp", {"-c", "-R", src, tree}) != 0) fail("could not clone " + src + " into " + tree);
    }
    // QEMU's configure needs a Python >= 3.12 that can make venvs: uv's
    // own, by its real path (the ~/.local/bin/python3 symlink cannot).
    proc::Options quiet;
    quiet.quiet = true;
    const QString py = proc::capture("uv", {"python", "find", ">=3.12"}, quiet).text();
    if (py.isEmpty()) fail("no Python >= 3.12 for QEMU's configure: uv python install 3.12");
    proc::Options o;
    o.cwd = emu;
    o.env["PYTHON"] = py;
    if (proc::run("sh", {"scripts/build-qemu-sh4.sh", tree}, o) != 0) fail("the CDJ's QEMU did not build (" + tree + ")");
    if (proc::run("sh", {"scripts/build-cdj-gui-run.sh"}, o) != 0) fail("the CDJ's GUI core did not build");
    if (!QFileInfo::exists(cdj::python())) {
        if (proc::run("uv", {"venv", "--quiet", "--python", ">=3.10", emu + "/.venv"}, o) != 0 ||
            proc::run("uv", {"pip", "install", "--quiet", "--python", cdj::python(), "-r", emu + "/requirements.txt"}, o) != 0)
            fail("could not make the emulator's venv (" + emu + "/.venv)");
    }
    print() << "built: " << cdj::qemu() << "\n       " << emu << "/bin/cdj-gui-run\n       " << cdj::python() << "\n";
    if (!QFileInfo::exists(cdj::firmware() + "/nxs/main-firmware.bin"))
        print() << "next: " << tool("cdj firmware ~/Downloads/C2KNXS.UPD") << "\n";
    return 0;
}

int up(cli::Args& a) {
    cdj::Cdj c(a.take("NAME"));
    a.done();
    cdj::Cdj::Up o;
    o.link = a.value("link");
    if (a.has("sd")) o.sd = QFileInfo(a.value("sd")).absoluteFilePath();
    if (a.has("usb")) o.usb = QFileInfo(a.value("usb")).absoluteFilePath();
    o.testTrack = a.has("test-track");
    o.window = a.has("window");
    o.dspModel = a.has("dsp-model");
    c.up(o);
    return 0;
}

int list(cli::Args& a) {
    a.done();
    const QStringList names = cdj::Cdj::all();
    if (names.isEmpty()) { print() << "no CDJs\n"; return 0; }
    for (const QString& n : names) {
        cdj::Cdj c(n);
        const qint64 pid = c.pid();
        print() << n.leftJustified(20) << " " << (pid ? QString("running (pid %1)").arg(pid) : QString("stopped")).leftJustified(20)
                << " " << c.mac() << (c.link().isEmpty() ? QString(", no cable") : ", on link " + c.link()) << "\n";
    }
    return 0;
}

int status(cli::Args& a) {
    cdj::Cdj c(a.take("NAME"));
    a.done();
    if (!c.exists()) fail("no CDJ " + c.name() + ": " + tool("cdj up " + c.name()));
    print() << c.name() << ": " << (c.running() ? QString("running, pid %1").arg(c.pid()) : QString("stopped")) << ", "
            << c.mac() << (c.link().isEmpty() ? QString(", no cable") : ", on link " + c.link()) << "\n"
            << "  run directory: " << c.runDir() << "\n  log: " << c.log() << "\n";
    return c.dev({"status"});
}

int shot(cli::Args& a) {
    cdj::Cdj c(a.take("NAME"));
    const QString png = QFileInfo(a.take("FILE.png")).absoluteFilePath();
    a.done();
    c.requireRunning();
    QFile::remove(png); // the emulator's own screenshot refuses to overwrite one
    const proc::Result r = cdj::captureTool({"tools.cdj_main.dev", c.runDir(), "screenshot", png}, true);
    if (!r.ok()) fail(c.name() + ": no screenshot: " + QString::fromUtf8(r.out + r.err).trimmed());
    print() << png << "\n";
    return 0;
}

int press(cli::Args& a) {
    cdj::Cdj c(a.take("NAME"));
    const QString key = a.take("KEY");
    a.done();
    c.requireRunning();
    QStringList args{"press", key};
    if (a.has("hold-ms")) args << "--hold-ms" << QString::number(a.number("hold-ms", 100));
    return c.dev(args);
}

int rotary(cli::Args& a) {
    cdj::Cdj c(a.take("NAME"));
    const QString steps = a.take("STEPS");
    a.done();
    c.requireRunning();
    return c.dev({"rotary", steps});
}

int rm(cli::Args& a) {
    cdj::Cdj c(a.take("NAME"));
    a.done();
    if (!c.exists() && !QFileInfo::exists(c.dir())) fail("no CDJ " + c.name());
    c.rm();
    return 0;
}

int run(cli::Args& a) {
    cdj::Cdj c(a.take("NAME"));
    a.done();
    return c.serve();
}

} // namespace

namespace commands {

void addCdj(cli::Registry& r) {
    r.group("cdj", "emulated CDJ-2000NXSs, on Pioneer's own firmware, on the decks' links");
    r.add({
        .group = "cdj", .name = "firmware", .synopsis = "C2KNXS.UPD",
        .summary = "split and extract the NXS updater, outside every checkout",
        .help = "The CDJ-2000NXS updater (1.44: C2KNXS.UPD), which Pioneer gives its owners.\n"
                "The emulator's extractors check every CRC and checksum and write what it\n"
                "boots into ~/.pi-qemu/cdj/firmware/nxs/: never into a repository.",
        .run = firmwareVerb,
    });
    r.add({
        .group = "cdj", .name = "build",
        .summary = "build this checkout's emulator: QEMU with the CDJ boards, the GUI core",
        .help = "In cdj2000-emulator/'s ignored build/ and bin/. QEMU is fetched once, at the\n"
                "revision the emulator's patches take, into ~/.pi-qemu/cdj/. In a worktree,\n"
                "the submodule is the main checkout's, borrowed by `worktree prepare`.",
        .run = build,
    });
    r.add({
        .group = "cdj", .name = "up", .synopsis = "NAME",
        .summary = "boot a CDJ, headless, and wait for it on its link",
        .help = "Its state is in ~/.pi-qemu/cdj/NAME/. On a link, it is a member of its own\n"
                "with a MAC from its name, and `up` waits for its first keep-alive.\n"
                "\n"
                "It runs Pioneer's DSP code by default, interpreted: a loaded track does not\n"
                "play in real time. --dsp-model runs the emulator's behavioural DSP instead,\n"
                "which plays a track in real time (its beats at its tempo) and executes none\n"
                "of the DSP's code: no audio either way.",
        .options = {{"link", "NET", "its Ethernet on link NET, with the decks there (deck up --link)"},
                    {"sd", "IMG", "a FAT32 card image; its writes are thrown away"},
                    {"usb", "IMG", "a FAT32 stick image; its writes are thrown away"},
                    {"test-track", "", "a card with a 10 s test WAV on it"},
                    {"dsp-model", "", "the behavioural DSP: tracks play in real time"},
                    {"window", "", "show its screen and panel on the Mac"}},
        .run = up,
    });
    r.add({.group = "cdj", .name = "list", .summary = "the CDJs, running or not", .run = list});
    r.add({.group = "cdj", .name = "status", .synopsis = "NAME", .summary = "what a CDJ is doing: the emulator's own status",
           .run = status});
    r.add({.group = "cdj", .name = "shot", .synopsis = "NAME FILE.png", .summary = "its 480x234 screen into FILE.png",
           .run = shot});
    r.add({
        .group = "cdj", .name = "press", .synopsis = "NAME KEY",
        .summary = "press a panel key: play, cue, link, usb, sd, enter, back, menu, ...",
        .help = "The emulator's NXS key names (tools/cdj_main/nxs_panel.py); `enter` is the\n"
                "browse encoder's push and `back` is RETURN. 100 ms unless --hold-ms.",
        .options = {{"hold-ms", "MS", "how long the key stays down"}},
        .run = press,
    });
    r.add({.group = "cdj", .name = "rotary", .synopsis = "NAME STEPS", .summary = "turn the browse encoder by STEPS (signed)",
           .run = rotary});
    r.add({.group = "cdj", .name = "rm", .synopsis = "NAME", .summary = "stop a CDJ and delete its state", .run = rm});
    r.add({.group = "cdj", .name = "run", .synopsis = "NAME",
           .summary = "the CDJ behind `cdj up` (it starts this): the emulator, and its port on the link", .run = run});
}

} // namespace commands
