// pi-qemu image: a deck's dev card, from stock Raspberry Pi OS.

#include "cli/cli.h"
#include "cli/commands/commands.h"

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
                "remaking the golden pair agents' instances start from: the person's call. One\n"
                "build of a deck runs at a time.\n"
                "Needs QEMU (pi-qemu/build.sh), Docker with ~6 GB free, mtools, and the key the\n"
                "cards trust (SSH_KEY).",
        .options = {
            {"rebuild-base", {}, "make the base card again, cached or not"},
            {"no-window", {}, "no build window (BUILD_WINDOW=0 does the same)"},
        },
        .run = build,
    });
}

} // namespace commands
