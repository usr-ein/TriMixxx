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

} // namespace files
