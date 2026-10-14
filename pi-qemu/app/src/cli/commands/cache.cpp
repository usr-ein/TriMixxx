// pi-qemu cache: every cache and archive pi-qemu keeps, and Docker's, from
// one place (cache/cache.h).

#include "cli/cli.h"
#include "cli/commands/commands.h"

#include "cache/cache.h"
#include "util/paths.h"

#include <QDir>
#include <QFile>

namespace commands {

namespace {

const cli::Option kRoot{"root", "DIR", "another checkout's, a scratch copy's (default: the main checkout's)"};

} // namespace

void addCache(cli::Registry& r) {
    r.group("cache", "every cache and archive, Docker's too: what it is, its size, clearing the cache", "list");
    const QString help =
        "Two git-ignored folders at the main checkout's root, which every worktree uses:\n"
        "  .cache/     what pi-qemu makes again: the stock card, image build's cards, the\n"
        "              golden snapshot, QEMU's build. Clearing it is fine: prune.\n"
        "  archives/   what is hard to make again: the releases, the NXS firmware, the USB\n"
        "              sticks, the decks' identities. No command deletes from it.\n"
        "The decks' and CDJs' own state stays in ~/.pi-qemu (short paths, for their\n"
        "sockets), linked from .cache/. Docker's caches stay in its VM, listed only: they\n"
        "are read while its VM runs, and shown as last read while it sleeps.";
    r.add({.group = "cache", .name = "list", .summary = "everything, with its size, what it is for, and what makes it again",
           .help = help + "\n\n`pi-qemu cache` alone is this.",
           .run = [](cli::Args& a) { a.done(); cache::list(); return 0; }});
    r.add({.group = "cache", .name = "prune", .synopsis = "KIND... | --all",
           .summary = "clears those kinds of the cache, unless something uses them; never the archives",
           .help = help + "\n\nKINDs: stock, base, build, golden, qemu, cdj-qemu. Refused, with nothing\n"
                          "cleared, while something uses one: a running deck (QEMU), an image or release\n"
                          "build, deck golden, deck up, cdj build, build.sh.",
           .options = {{"all", "", "every kind of the cache"}, kRoot},
           .run = [](cli::Args& a) {
               QStringList kinds = a.takeAll();
               if (a.has("all") == !kinds.isEmpty()) cli::usage("name the KINDs to clear, or --all");
               if (a.has("all")) kinds = {"stock", "base", "build", "golden", "qemu", "cdj-qemu"};
               if (a.has("root")) {
                   const QString root = QDir(a.value("root")).absolutePath();
                   qputenv("PI_QEMU_CACHE", QFile::encodeName(root + "/.cache"));
                   qputenv("PI_QEMU_ARCHIVES", QFile::encodeName(root + "/archives"));
               }
               cache::prune(kinds);
               return 0;
           }});
    r.add({.group = "cache", .name = "migrate", .synopsis = "[--undo]",
           .summary = "moves what pi-qemu kept elsewhere into .cache/ and archives/, once",
           .help = help + "\n\nFrom pi-qemu/.cache, pi-qemu/qemu/.build, pi-qemu/release/out and\n"
                          "~/.pi-qemu/cdj (its firmware, QEMU source, and what people left there),\n"
                          "and a worktree's own QEMU build into its .cache/qemu. Renames, on one volume:\n"
                          "instant, and clones keep sharing. Refused, with nothing moved, while a deck, a\n"
                          "CDJ, a build or another pi-qemu runs. Twice is the same as once: what is in\n"
                          "both places stays where it was, said. Each move is recorded in\n"
                          ".cache/moves.json, which --undo follows back. With --root, only what is under\n"
                          "DIR moves: never ~/.pi-qemu/cdj (PI_QEMU_CDJ under DIR stands for it).",
           .options = {{"undo", "", "move back what it moved"}, kRoot},
           .run = [](cli::Args& a) {
               a.done();
               cache::migrate(QDir(a.value("root", paths::mainCheckout())).absolutePath(), a.has("undo"));
               return 0;
           }});
}

} // namespace commands
