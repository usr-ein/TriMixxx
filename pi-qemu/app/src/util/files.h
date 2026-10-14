#pragma once
// Files as the tools need them: whole reads and writes, and cards copied as
// APFS clones -- a copy of an 8 GB card in no time and no space.

#include <QByteArray>
#include <QString>

namespace files {

QByteArray read(const QString& path);            // fails if it cannot
void write(const QString& path, const QByteArray& data, bool privateFile = false); // 0600 when private
void clone(const QString& from, const QString& to);  // replaces `to`; a plain copy where clones can't be
bool inUse(const QString& path);                 // some process has it open (lsof)

// A card image's zeros given back to the disk. QEMU and Docker's share of the
// Mac's disk write zeros out in full: a release card took 12.6 GB on disk for
// 1.1 GB of data, a deck's 8 GB card 6.6 for 3.7. Every whole 64 KiB block of
// zeros becomes a hole (F_PUNCHHOLE), in an APFS clone of the file that is
// read back equal to it before it takes the file's place, times kept. Its
// other clones keep their blocks: a family of clones gives a block back only
// once every member has. Never on a file in use.
struct Sparse {
    bool    ok = false;
    QString error;      // why not, when not ok: the file is then as it was
    qint64  before = 0; // bytes on disk, before and after
    qint64  after = 0;
};
Sparse sparsify(const QString& path);

} // namespace files
