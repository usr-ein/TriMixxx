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
// A card shipped as .xz (a release's) decompressed into `to`, sparse: xz
// unpacks it and dd writes it, seeking over each 1 MiB of zeros instead
// (conv=sparse), so it takes its data's room on the Mac's disk. Fails if it can't.
void unxz(const QString& from, const QString& to);
// `dir`, if it is there, given a .gitignore of "*": git ignores the folder
// whatever commit is checked out, the root's .gitignore or not (.cache/,
// archives/: the firmware and people's sticks are never committed).
void ignoreItself(const QString& dir);

} // namespace files
