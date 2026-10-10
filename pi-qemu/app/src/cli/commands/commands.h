#pragma once
// Each group's verbs, one file each.

namespace cli { class Registry; }

namespace commands {

void addDeck(cli::Registry& r);     // deck.cpp
void addLink(cli::Registry& r);     // link.cpp
void addCdj(cli::Registry& r);      // cdj.cpp
void addImage(cli::Registry& r);    // image.cpp
void addRelease(cli::Registry& r);  // release.cpp
void addWorktree(cli::Registry& r); // worktree.cpp
void addRun(cli::Registry& r);      // run.cpp: run, build-log

inline void addAll(cli::Registry& r) {
    addDeck(r);
    addLink(r);
    addCdj(r);
    addImage(r);
    addRelease(r);
    addWorktree(r);
    addRun(r);
}

} // namespace commands
