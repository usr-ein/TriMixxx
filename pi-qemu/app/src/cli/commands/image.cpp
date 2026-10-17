// pi-qemu image: a deck's dev card, from stock Raspberry Pi OS.

#include "cli/cli.h"
#include "cli/commands/commands.h"

#include "pipeline/golden.h"
#include "pipeline/imagebuild.h"
#include "util/buildlog.h"
#include "util/paths.h"

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
    golden::Options o;
    o.start = !a.has("check");
    o.wait = !a.has("background") && !a.has("check");
    o.retry = a.has("retry");
    const golden::State s = golden::ensure(o).state;
    return s == golden::State::Current || (s == golden::State::Remaking && !o.wait) ? 0
         : s == golden::State::Stale                                                ? 3
                                                                                    : 1;
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
        .group = "image", .name = "golden", .synopsis = "[--background | --check] [--retry]",
        .summary = "the golden every emulated deck starts from, made main's latest if it isn't",
        .help = "origin/main is fetched and compared with what the golden holds (.cache/golden/trimixxx0.json),\n"
                "in what goes onto a deck (pi-qemu/deploy, pi-qemu/image, pi_config, mixxx, mixxx_config,\n"
                "trimixxx-launcher, doom). When main has moved on there, the golden is made again: image\n"
                "build trimixxx0 in the main checkout, as a process of its own (a few minutes; its log:\n"
                ".cache/build/trimixxx0.log), then the golden from its card. This waits for it, or for one\n"
                "running already, and says how it went; deck up does the same before a deck starts from\n"
                "the golden, and crew merge starts one after a merge (--background).\n"
                "\n"
                "Only main's code: the main checkout must hold it for the deck (its files, untracked ones\n"
                "too, and its submodules, as origin/main has them; its branch does not matter), or the\n"
                "golden stays as it is and this says why. A main whose build failed is not tried again\n"
                "by itself: --retry. Exit status: 0 main's latest (or a remake started, --background),\n"
                "3 to be made again (--check), 1 it could not be.",
        .options = {
            {"background", {}, "start the remake if it is needed, and return"},
            {"check", {}, "only say: main's latest, running, failed, or to be made again"},
            {"retry", {}, "a main whose build failed: try it again"},
        },
        .run = goldenVerb,
    });
}

} // namespace commands
