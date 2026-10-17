#pragma once
// pi-qemu's caches and archives, found from one place (`pi-qemu cache`): two
// git-ignored folders at the main checkout's root, which every worktree uses.
//
//   .cache/        what pi-qemu makes again: clearing it is fine (prune)
//     stock/         Raspberry Pi OS Lite, downloaded and unpacked (image build)
//     base/          image build's first boot and base step, per deck
//     build/         image build's cards, what each was built from, the builds' logs and attempts
//     golden/        trimixxx0.img + .state: every emulated deck starts from them, main's
//                    latest (pipeline/golden); .json, what code they hold
//     qemu/          the decks' QEMU, patched: its source and build (build.sh qemu)
//     cdj-qemu/      the CDJ emulator's QEMU source, pinned (cdj build)
//     docker.json    the last reading of Docker's caches, for while its VM sleeps
//     moves.json     what `cache migrate` moved, for --undo
//     instances, cdj, links -> ~/.pi-qemu/...: the decks' and CDJs' own state,
//                    kept there for their sockets' short paths
//   archives/      what is hard to make again: no command deletes from it
//     releases/      every release built (release build, release card)
//     cdj-firmware/  the NXS firmware the CDJs boot (cdj firmware)
//     sticks/        USB sticks, for decks and CDJs: images, folders, .stick files
//     decks/         each deck's identity: ssh host keys, Wi-Fi (release card)
//     captures/      what each real deck ran before its first release card
//
// Docker's caches stay in its VM (Mixxx's build trees, the ccache, the cargo
// registry, layers): listed with the rest, never pruned here.

#include <QString>
#include <QStringList>
#include <QVector>

namespace cache {

enum class Shelf { Cache, Archive, State };

struct Kind {
    QString name;   // its folder's name; prune's word for it
    Shelf   shelf;
    QString path;
    QString what;   // what it holds
    QString remade; // what makes it again (cache), writes it (archive) or removes it (state)
};
QVector<Kind> kinds();

// Who is using what now: a running deck or CDJ, a pi-qemu command, build.sh,
// a process naming a kind's folder; and which cache kinds it needs.
struct User {
    QString     who;
    QStringList kinds;
};
QVector<User> users();

// `pi-qemu cache`: everything, with its size, what it is, how it comes back.
void list();

// `pi-qemu cache prune`: those kinds of the cache, all of it, or fails,
// changing nothing, while something uses one of them. Never the archives.
// `using`: who uses what (users(), unless a test says).
void prune(const QStringList& kinds);
void prune(const QStringList& kinds, const QVector<User>& using_);

// `pi-qemu cache migrate`: what is still where it was before .cache/ and
// archives/, moved there, under `root` (the main checkout) and in the code
// checkout when it is a worktree. Renames, on one volume: clones keep
// sharing. Fails, changing nothing, while a deck, a CDJ or a build runs.
// Twice is the same as once. `undo` moves back what it moved.
void migrate(const QString& root, bool undo);
void migrate(const QString& root, bool undo, const QVector<User>& using_);

// Fails while a place pi-qemu used before holds what now belongs in .cache/
// or archives/, saying how to move it (paths::setLayoutCheck).
void requireMoved();

} // namespace cache
