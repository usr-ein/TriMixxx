#pragma once
// cache/'s own helpers: paths as the person reads them, the links to the
// decks' and CDJs' state, and the moves from where pi-qemu kept things before
// .cache/ and archives/.

#include <QString>
#include <QStringList>
#include <QVector>

namespace cache::places {

bool    there(const QString& path); // a file, a folder or a symlink, not followed
QString shown(const QString& path); // relative to the main checkout, or ~/...

// The decks' and CDJs' own state, in ~/.pi-qemu for their sockets' short
// paths, linked from .cache/: instances, cdj, links.
QStringList stateLinks(const QString& cacheDir);
void        linkState(const QString& cacheDir);

struct Move {
    QString from, to;
};
// Where things were, and where each goes: under `root` (the main checkout),
// from the CDJs' folder `cdjs`, and `checkout`'s own QEMU build when it is a
// worktree. Only what is there. `ours`: only what pi-qemu put there, not what
// people left in the CDJs' folder.
QVector<Move> planned(const QString& root, const QString& cdjs, const QString& checkout, bool ours = false);
// What pi-qemu left in its old places and isn't in its new one yet: what
// stops every command but `pi-qemu cache` until it moves.
QStringList waiting(const QString& root, const QString& cdjs);

} // namespace cache::places
