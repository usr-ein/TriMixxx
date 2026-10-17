// pi-qemu image: a deck's dev card, from stock Raspberry Pi OS.

#include "cli/cli.h"
#include "cli/commands/commands.h"

#include "pipeline/golden.h"
#include "pipeline/imagebuild.h"
#include "util/buildlog.h"
#include "util/paths.h"
#include "util/print.h"
#include "util/tool.h"

namespace {

int build(cli::Args& a) {
    const QString deck = a.takeOr("trimixxx0");
    a.done();
    imagebuild::Options o;
    o.rebuildBase = a.has("rebuild-base");
    const imagebuild::Lock lock(deck); // before its log is emptied: one build of a deck at a time
    return buildlog::run(imagebuild::logFile(deck), !a.has("no-window"), imagebuild::plan(deck),
                         [&] { imagebuild::build(deck, o); });
}

int goldenVerb(cli::Args& a) {
    a.done();
    if (a.has("clear")) {
        golden::clear();
        return 0;
    }
    const bool retry = a.has("retry");
    golden::State s;
    if (a.has("keeper")) {
        s = golden::keep(retry).state;
    } else if (a.has("background")) {
        s = golden::keepInBackground(retry).state;
        if (s == golden::State::Stale) s = golden::State::Remaking; // its keeper has it
    } else if (a.has("check")) {
        golden::Options o;
        o.start = o.wait = false;
        o.retry = retry;
        s = golden::ensure(o).state;
        if (s == golden::State::Stale) print() << "  " << tool("image golden") << " makes it now, or the next deck up\n";
    } else {
        golden::Options o;
        o.retry = retry;
        s = golden::ensure(o).state;
    }
    return s == golden::State::Current || s == golden::State::Remaking ? 0 : s == golden::State::Stale ? 3 : 1;
}

} // namespace

namespace commands {

void addImage(cli::Registry& r) {
    r.group("image", "a deck's dev card, built from stock Raspberry Pi OS on an emulated Pi");
    r.add({
        .group = "image", .name = "build", .synopsis = "[DECK] [options]",
        .summary = "the card for DECK (trimixxx0): stock Pi OS, its seed, then every deploy step",
        .help = "DECK names the card's hostname and its unit file (mixxx_config/units/DECK.json).\n"
                "The build runs in an instance of its own, build-DECK (deck ssh reaches it), and\n"
                "the card lands in the main checkout's .cache/build/DECK.img, with its log beside\n"
                "it, DECK.log, which a window follows. The first boot and the base step are cached\n"
                "in .cache/base/ while their inputs are unchanged. trimixxx0's build ends by\n"
                "remaking the golden pair agents' instances start from: by hand, the person's call\n"
                "(image golden makes it from main's code). One build of a deck runs at a time.\n"
                "Needs QEMU (pi-qemu/build.sh), Docker with ~6 GB free, mtools, and the key the\n"
                "cards trust (SSH_KEY).",
        .options = {
            {"rebuild-base", {}, "make the base card again, cached or not"},
            {"no-window", {}, "no build window (BUILD_WINDOW=0 does the same)"},
        },
        .run = build,
    });
    r.add({
        .group = "image", .name = "golden", .synopsis = "[--background | --check | --clear] [--retry]",
        .summary = "the golden every emulated deck starts from, made main's latest if it isn't",
        .help = "origin/main is fetched and compared with what the golden holds (.cache/golden/trimixxx0.json),\n"
                "in what goes onto a deck (pi-qemu/deploy, pi-qemu/image, image build's own code, pi_config,\n"
                "mixxx, mixxx_config, trimixxx-launcher, doom). When main has moved on there, the golden\n"
                "is made again: image build trimixxx0 in the main checkout, as a process of its own (a\n"
                "few minutes; its log: .cache/build/trimixxx0.log), then the golden from its card. This\n"
                "waits for it, or for one running already, and says how it went. origin/main is read\n"
                "again after each wait: a main that moved on meanwhile is the one it is made for. deck up\n"
                "does the same before a deck starts from the golden; crew merge leaves it to a keeper\n"
                "(--background).\n"
                "\n"
                "Only main's code: the main checkout must hold it for the deck (its files, untracked ones\n"
                "too, and its submodules, as origin/main has them; its branch does not matter), or the\n"
                "golden stays as it is and this says why. And only by main's pi-qemu, the one on PATH.\n"
                "A main whose build failed is not tried again by itself: --retry. A remake running past\n"
                "the time it may take (60 min) looks stuck: nobody waits for it, and decks start from\n"
                "the last golden until --clear stops it (the nightman's or a person's), once its log has\n"
                "been quiet 10 min: its process group, then its build machine.\n"
                "\n"
                "Exit status: 0 main's latest, or being made (--background, --check); 3 to be made\n"
                "again (--check); 1 it can't be now (failed, stuck, the main checkout).",
        .options = {
            {"background", {}, "leave it to a keeper, which goes on until the golden is main's latest; return"},
            {"check", {}, "only say: main's latest, being made, failed, stuck, or to be made again"},
            {"clear", {}, "stop a remake that looks stuck, its log quiet 10 min (the nightman's or a person's call)"},
            {"retry", {}, "a main whose build failed: try it again"},
            {"keeper", {}, "what --background runs: the same as no option, one keeper at a time"},
        },
        .run = goldenVerb,
    });
}

} // namespace commands
