#pragma once
// A git worktree of the repo, ready to build and deploy the deck's code: for
// agents (and people) working side by side, one worktree each. It acts on
// the checkout pi-qemu runs in (util/paths).
//
// A new worktree's submodule directories are empty. `prepare` makes mixxx/,
// its lib/prolink and mixxx_config/ttymidi linked worktrees of the MAIN
// checkout's submodule repositories, detached at the commits this worktree
// records: seconds, nothing downloaded, and -- unlike `git submodule update`,
// whose private clone dies with the worktree -- a commit or branch made in them
// is in the main checkout's repositories at once. So, to work on the Mixxx
// fork in a worktree: `git -C mixxx switch -c BRANCH` first, commit there, and
// the main checkout sees BRANCH.
//
// Each checkout builds Mixxx in its own Docker build tree (mixxx/checkout-id.sh),
// ~4 GB of Docker's disk, which `release` gives back with the submodule
// checkouts: git will not remove a worktree with checked-out submodules
// (without --force). `release` refuses if they hold uncommitted changes or
// commits no branch has.

namespace worktree {

void prepare(bool quiet = false); // quiet: no word when there is nothing to do
void status();
void release();

} // namespace worktree
