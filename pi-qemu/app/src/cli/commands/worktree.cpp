// pi-qemu worktree: a git worktree of the repo, ready to build and deploy.

#include "cli/cli.h"
#include "cli/commands/commands.h"

#include "worktree/worktree.h"

namespace commands {

void addWorktree(cli::Registry& r) {
    r.group("worktree", "a git worktree's submodules and Docker build trees, for working side by side");
    const QString help = "It acts on the checkout it runs in. A new worktree's submodules (mixxx, its\n"
                         "lib/prolink, mixxx_config/ttymidi) are empty: `prepare` makes them linked\n"
                         "worktrees of the main checkout's, at the commits this worktree records, so a\n"
                         "branch made in them is the main checkout's at once. Work on the fork with\n"
                         "`git -C mixxx switch -c BRANCH` first. `release`, before `git worktree remove`,\n"
                         "gives back the Docker build tree (~4 GB) and the submodule checkouts; it refuses\n"
                         "if they hold uncommitted changes or commits no branch has.";
    r.add({.group = "worktree", .name = "prepare", .summary = "check out the submodules the deck is built from", .help = help,
           .run = [](cli::Args& a) { a.done(); worktree::prepare(); return 0; }});
    r.add({.group = "worktree", .name = "status", .summary = "what this checkout has: submodules, branches, build tree", .help = help,
           .run = [](cli::Args& a) { a.done(); worktree::status(); return 0; }});
    r.add({.group = "worktree", .name = "release", .summary = "before removing the worktree: its submodules and build trees, gone",
           .help = help, .run = [](cli::Args& a) { a.done(); worktree::release(); return 0; }});
}

} // namespace commands
