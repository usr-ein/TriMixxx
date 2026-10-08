// pi-qemu deck: emulated decks' lives, and driving any deck, emulated or real.

#include "cli/cli.h"
#include "cli/commands/commands.h"

#include "decks/console.h"
#include "decks/deck.h"
#include "decks/emulateddeck.h"
#include "decks/instances.h"
#include "decks/readiness.h"
#include "decks/recorder.h"
#include "decks/touchscreen.h"
#include "s3/controls.h"
#include "util/fail.h"
#include "util/process.h"
#include "util/tool.h"

#include <QDateTime>
#include <QFileInfo>
#include <QTextStream>

namespace {

QTextStream& out() {
    static QTextStream s(stdout);
    return s;
}

const cli::Option kHost{"host", "ALIAS", decks::hostOptionHelp()};

// How the person names this deck, for advice: NAME, or --host ALIAS.
QString spelled(const Deck& d) { return d.emulated() ? d.name() : "--host " + d.name(); }

int up(cli::Args& a) {
    Instance i(a.take("NAME"));
    a.done();
    Instance::Up o;
    o.window = a.has("window");
    o.fresh = a.has("fresh");
    o.boot = a.has("boot");
    if (a.has("from")) o.from = QFileInfo(a.value("from")).absoluteFilePath();
    o.runArgs = a.afterDashes();
    i.up(o);
    return 0;
}

int list(cli::Args& a) {
    a.done();
    const QStringList names = Instance::all();
    if (names.isEmpty()) { out() << "no instances\n"; return 0; }
    for (const QString& n : names) {
        Instance i(n);
        const qint64 pid = i.pid();
        const QString state = pid ? QString("running (pid %1)").arg(pid)
                                  : QFileInfo::exists(i.state()) ? "suspended" : "stopped";
        out() << n.leftJustified(20) << " ssh port " << (pid ? QString::number(i.sshPort()) : "-").leftJustified(5)
              << " " << state << "\n";
    }
    return 0;
}

template <void (Instance::*Fn)()>
int lifecycle(cli::Args& a) {
    Instance i(a.take("NAME"));
    a.done();
    if (!i.exists()) fail("no instance " + i.name());
    (i.*Fn)();
    return 0;
}

int golden(cli::Args& a) {
    const QString card = a.takeOr({});
    a.done();
    Instance::golden(card);
    return 0;
}

int ssh(cli::Args& a) {
    auto d = decks::target(a);
    d->requireUp();
    proc::Options o;
    o.input = proc::Input::Inherit;
    return proc::run("ssh", d->ssh().args() + a.takeAll(), o);
}

int ready(cli::Args& a) {
    auto d = decks::target(a);
    const int seconds = a.takeNumber("SECONDS", 120);
    a.done();
    d->requireUp();
    readiness::waitMixxx(d->ssh(), seconds, spelled(*d), [&d] { d->requireUp(); });
    const proc::Result pid = d->ssh().capture("pgrep -xo mixxx");
    out() << d->name() << ": Mixxx is ready (sound open, the S3 connected; pid " << pid.text() << ")\n";
    return 0;
}

int env(cli::Args& a) {
    Instance i(a.take("NAME"));
    a.done();
    if (!QFileInfo::exists(i.binDir() + "/ssh")) fail("no instance " + i.name() + " (" + tool("deck up " + i.name()) + ")");
    out() << "export PATH=" << proc::quote(i.binDir()) << ":\"$PATH\"\n";
    return 0;
}

int consoleVerb(cli::Args& a) {
    EmulatedDeck d(a.take("NAME"));
    const QStringList commands = a.takeAll();
    if (commands.isEmpty()) cli::usage("missing COMMAND");
    d.requireUp();
    console::run(d.instance().consoleSocket(), commands);
    return 0;
}

int shot(cli::Args& a) {
    auto d = decks::target(a);
    const QString png = QFileInfo(a.take("FILE.png")).absoluteFilePath();
    a.done();
    d->requireUp();
    d->shot(png);
    out() << png << "\n";
    return 0;
}

// press, down, up, jog, touch, browse, tempo, midi: the S3's controls.
int control(cli::Args& a, const QString& verb) {
    auto d = decks::target(a);
    d->requireUp();
    out() << d->controls(QStringList{verb} + a.takeAll()) << "\n";
    return 0;
}

// leds, stick, power, save, status: only an emulated deck has these.
int board(cli::Args& a, const QString& verb) {
    EmulatedDeck d(a.take("NAME"));
    QStringList words{verb};
    words << a.takeAll();
    if (verb == "save") {
        if (words.size() != 2) cli::usage("save NAME FILE");
        words[1] = QFileInfo(words[1]).absoluteFilePath();
    }
    out() << d.controls(words) << "\n";
    return 0;
}

int tap(cli::Args& a) {
    auto d = decks::target(a);
    const int x = a.takeNumber("X"), y = a.takeNumber("Y");
    a.done();
    touchscreen::tap(*d, x, y);
    return 0;
}

int longpress(cli::Args& a) {
    auto d = decks::target(a);
    const int x = a.takeNumber("X"), y = a.takeNumber("Y"), ms = a.takeNumber("MS", 800);
    a.done();
    touchscreen::longpress(*d, x, y, ms);
    return 0;
}

int drag(cli::Args& a) {
    auto d = decks::target(a);
    const int x1 = a.takeNumber("X1"), y1 = a.takeNumber("Y1"), x2 = a.takeNumber("X2"), y2 = a.takeNumber("Y2");
    const int ms = a.takeNumber("MS", 250);
    a.done();
    touchscreen::drag(*d, x1, y1, x2, y2, ms);
    return 0;
}

int key(cli::Args& a) {
    auto d = decks::target(a);
    touchscreen::keys(*d, a.takeAll());
    return 0;
}

int where(cli::Args& a) {
    auto d = decks::target(a);
    a.done();
    out() << touchscreen::where(*d) << "\n";
    return 0;
}

int record(cli::Args& a) {
    auto d = decks::target(a);
    const int seconds = a.takeNumber("SECONDS", 10);
    const QString wav = a.takeOr(QString("/tmp/deck-%1.wav").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")));
    a.done();
    recorder::record(*d, seconds, wav);
    return 0;
}

} // namespace

namespace commands {

void addDeck(cli::Registry& r) {
    r.group("deck", "emulated and real decks: start, drive, deploy, update");

    // ---- emulated decks' lives ----
    r.add({
        .group = "deck", .name = "up", .synopsis = "NAME [options] [-- RUN_ARGS]",
        .summary = "an emulated deck: restored from the golden snapshot in seconds, or resumed",
        .help = "A new NAME gets clones of the golden card and its saved machine, and wakes up\n"
                "booted with Mixxx running. A stopped NAME resumes; one that is running is only\n"
                "reported, so call it freely. Headless and silent unless --window; RUN_ARGS go\n"
                "to `run` as they are, e.g. -- --audio wav:/abs/out.wav (never --audio\n"
                "speakers unless the person at the laptop asked for sound).",
        .options = {
            {"window", {}, "the Pi's screen and the deck's controls in windows"},
            {"fresh", {}, "start over from the golden pair (its card is replaced)"},
            {"boot", {}, "boot its card rather than resume a saved machine"},
            {"from", "CARD", "a card of your own (a clone of it), booted"},
        },
        .run = up,
    });
    r.add({.group = "deck", .name = "list", .summary = "the emulated decks: their ssh ports, running or not", .run = list});
    r.add({.group = "deck", .name = "stop", .synopsis = "NAME", .summary = "suspend: the machine saved, then off (up resumes it)",
           .run = lifecycle<&Instance::stop>});
    r.add({.group = "deck", .name = "down", .synopsis = "NAME", .summary = "a clean poweroff", .run = lifecycle<&Instance::down>});
    r.add({.group = "deck", .name = "kill", .synopsis = "NAME", .summary = "pull the plug", .run = lifecycle<&Instance::kill>});
    r.add({.group = "deck", .name = "rm", .synopsis = "NAME", .summary = "pull the plug and delete it", .run = lifecycle<&Instance::remove>});
    r.add({
        .group = "deck", .name = "golden", .synopsis = "[CARD]",
        .summary = "remake the golden pair every new instance starts from (the person's call)",
        .help = "CARD (default pi-qemu/.cache/build/trimixxx0.img, image build's) is booted\n"
                "headless until Mixxx plays, then saved with its machine into\n"
                "pi-qemu/.cache/golden/trimixxx0.img and .state. Running instances keep theirs.",
        .run = golden,
    });

    // ---- reaching a deck ----
    r.add({
        .group = "deck", .name = "ssh", .synopsis = "TARGET [COMMAND...]",
        .summary = "a shell on the deck, or a command",
        .options = {kHost}, .optionsEnd = 1, .run = ssh,
    });
    r.add({
        .group = "deck", .name = "ready", .synopsis = "TARGET [SECONDS]",
        .summary = "wait until Mixxx runs, its sound open and the S3 connected (default 120 s)",
        .options = {kHost}, .run = ready,
    });
    r.add({
        .group = "deck", .name = "env", .synopsis = "NAME",
        .summary = "the export that makes plain ssh and scp reach NAME (and `deck`), for a shell of your own",
        .run = env,
    });
    r.add({
        .group = "deck", .name = "console", .synopsis = "NAME COMMAND...",
        .summary = "commands over the serial console, without ssh (each COMMAND one line)",
        .optionsEnd = 1, .run = consoleVerb,
    });
    r.add({.group = "deck", .name = "status", .synopsis = "NAME", .summary = "the board: the Pi, the S3 link, its wiring, its ports",
           .run = [](cli::Args& a) { return board(a, "status"); }});

    // ---- the screen ----
    r.add({
        .group = "deck", .name = "shot", .synopsis = "TARGET FILE.png",
        .summary = "the deck's screen: QEMU's own (emulated), scrot (real)",
        .options = {kHost}, .run = shot,
    });
    r.add({.group = "deck", .name = "tap", .synopsis = "TARGET X Y", .summary = "a fingertip on that pixel",
           .options = {kHost}, .run = tap});
    r.add({.group = "deck", .name = "longpress", .synopsis = "TARGET X Y [MS]", .summary = "a held touch (default 800 ms: a long press)",
           .options = {kHost}, .run = longpress});
    r.add({.group = "deck", .name = "swipe", .synopsis = "TARGET X1 Y1 X2 Y2 [MS]",
           .summary = "a drag (default 250 ms); left to right is BACK", .options = {kHost}, .run = drag});
    r.add({.group = "deck", .name = "flick", .synopsis = "TARGET X1 Y1 X2 Y2 [MS]",
           .summary = "the same drag, for a kinetic scroll", .options = {kHost}, .run = drag});
    r.add({.group = "deck", .name = "key", .synopsis = "TARGET KEYSYM...", .summary = "X key events (Down, Return, ...)",
           .options = {kHost}, .optionsEnd = 1, .run = key});
    r.add({.group = "deck", .name = "where", .synopsis = "TARGET", .summary = "where the pointer is",
           .options = {kHost}, .run = where});

    // ---- the S3's controls ----
    const QString controlsHelp = "As the deck's S3 sends them, on its wiring (mixxx_config/units/<deck>.json):\n" +
                                 s3::controlsHelp();
    struct C { const char* verb; const char* synopsis; const char* summary; };
    for (const C c : {C{"press", "TARGET CONTROL [MS]", "press and release a control (default 80 ms)"},
                      C{"down", "TARGET CONTROL", "hold a control down"},
                      C{"up", "TARGET CONTROL", "let it go"},
                      C{"jog", "TARGET TICKS", "turn the platter; + is clockwise, 12960 ticks per turn"},
                      C{"touch", "TARGET on|off", "the platter's touch sensor (scratch)"},
                      C{"browse", "TARGET N", "the track encoder, N detents; + is up"},
                      C{"tempo", "TARGET VALUE|center", "the tempo fader: 0..16383, 8192 the middle"},
                      C{"midi", "TARGET HEX...", "raw MIDI bytes, as the S3's (midi trimixxx0 90 3C 7F)"}}) {
        const QString verb = c.verb;
        r.add({.group = "deck", .name = verb, .synopsis = c.synopsis, .summary = c.summary, .help = controlsHelp,
               .options = {kHost}, .optionsEnd = 1, .run = [verb](cli::Args& a) { return control(a, verb); }});
    }
    r.add({
        .group = "deck", .name = "record", .synopsis = "TARGET [SECONDS] [OUT.wav]",
        .summary = "Mixxx's main mix, recorded by Mixxx for SECONDS (10), fetched (/tmp/deck-<time>.wav)",
        .options = {kHost}, .run = record,
    });

    // ---- an emulated deck's own ----
    r.add({.group = "deck", .name = "leds", .synopsis = "NAME", .summary = "what the deck's lights show now (JSON)",
           .run = [](cli::Args& a) { return board(a, "leds"); }});
    r.add({.group = "deck", .name = "stick", .synopsis = "NAME list | insert ID | unplug ID",
           .summary = "USB sticks: the Mac's own (diskN) or images, two slots, read-only",
           .run = [](cli::Args& a) { return board(a, "stick"); }});
    r.add({.group = "deck", .name = "power", .synopsis = "NAME on|off", .summary = "power: off pulls the plug",
           .run = [](cli::Args& a) { return board(a, "power"); }});
    r.add({.group = "deck", .name = "save", .synopsis = "NAME FILE",
           .summary = "the whole machine into FILE, then off (run --restore FILE CARD)",
           .run = [](cli::Args& a) { return board(a, "save"); }});
}

} // namespace commands
