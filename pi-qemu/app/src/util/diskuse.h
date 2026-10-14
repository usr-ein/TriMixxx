#pragma once
// How much of the Mac's disk a file or a tree takes, as APFS tells it. du's
// figure (allocated) counts the blocks a clone shares once for every clone:
// pi-qemu's cards are clones of each other (the golden, the build card, the
// decks' cards), so what one of them really holds is its private bytes, the
// blocks no clone shares, which deleting it alone gives back.

#include <QString>

namespace diskuse {

struct Use {
    qint64 files = 0;
    qint64 apparent = 0;  // the sizes the files say they have (sparse ones included)
    qint64 allocated = 0; // their blocks on disk, those a clone shares included
    qint64 own = 0;       // their blocks no clone shares: what deleting them alone frees
    Use& operator+=(const Use& o);
};

// A file, or a whole tree; symlinks are not followed. Nothing there: zeros.
Use of(const QString& path);

// The free bytes of the volume holding `path`.
qint64 freeBytes(const QString& path);

// "1.4 GB", "120 MB", "0 B": decimal, as Finder and df -H count.
QString text(qint64 bytes);

} // namespace diskuse
