// pi-qemu release: the A/B card every deck runs, its update bundle, and each
// deck's own card.

#include "cli/cli.h"
#include "cli/commands/commands.h"

#include "pipeline/release.h"

namespace {

int build(cli::Args& a) {
    a.done();
    release::BuildOptions o;
    o.rehearsal = a.value("rehearsal");
    o.keepSystem = a.has("keep-system");
    o.window = !a.has("no-window");
    return release::build(o);
}

int card(cli::Args& a) {
    const QString deck = a.take("DECK");
    a.done();
    release::card(deck, release::version(a.value("version")));
    return 0;
}

int faults(cli::Args& a) {
    a.done();
    release::faults(release::version(a.value("version")));
    return 0;
}

const cli::Option kVersion{"version", "X.Y.Z", "another release than HEAD's pi/vX.Y.Z tag says"};

} // namespace

namespace commands {

void addRelease(cli::Registry& r) {
    r.group("release", "the A/B card every deck runs, its update bundle, and each deck's own card");
    r.add({
        .group = "release", .name = "build", .synopsis = "[options]",
        .summary = "the release of HEAD's pi/vX.Y.Z tag: the system card, sealed into an A/B card and a bundle",
        .help = "The system is built as `image build trimixxx-release`, then sealed in the release\n"
                "container: pi-qemu/release/out/X.Y.Z/ gets trimixxx-X.Y.Z.img (every deck's card),\n"
                "trimixxx-X.Y.Z.raucb (the update), rootfs.squashfs, boot.vfat and manifest.txt.\n"
                "From a clean tree whose HEAD is tagged pi/vX.Y.Z (git tag -m \"TriMixxx X.Y.Z\"\n"
                "pi/vX.Y.Z: tags are signed); --rehearsal lets an untagged or dirty tree through,\n"
                "for trying releases on emulated decks, and its manifest says so. Its log,\n"
                "pi-qemu/.cache/build/trimixxx-release.log, is shown in a window.",
        .options = {
            {"rehearsal", "X.Y.Z", "a rehearsal of version X.Y.Z, from whatever the tree holds"},
            {"keep-system", {}, "seal the system card built last time, without building it again"},
            {"no-window", {}, "no build window (BUILD_WINDOW=0 does the same)"},
        },
        .run = build,
    });
    r.add({
        .group = "release", .name = "card", .synopsis = "DECK [--version X.Y.Z]",
        .summary = "DECK's card: the release card with the deck's identity on p7, to flash",
        .help = "The identity -- its name, its ssh host keys (kept, so known_hosts survives a\n"
                "reflash), its hotspot, the home Wi-Fi from image/secrets.env -- is kept in\n"
                "pi-qemu/.cache/decks/DECK/ and made the first time. The card is\n"
                "pi-qemu/release/out/X.Y.Z/DECK-X.Y.Z.img. The person flashes it.",
        .options = {kVersion},
        .run = card,
    });
    r.add({
        .group = "release", .name = "faults", .synopsis = "[--version X.Y.Z]",
        .summary = "broken updates made from the release, for the fault tests (PLAN.md §5.4)",
        .help = "out/X.Y.Z-nonet, -nossh, -noinitramfs, -panic and -hang, each signed like a real\n"
                "release, each to `deck ship --version X.Y.Z-NAME`.",
        .options = {kVersion},
        .run = faults,
    });
}

} // namespace commands
