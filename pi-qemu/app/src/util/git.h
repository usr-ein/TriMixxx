#pragma once
// git, as the pipelines ask it things: the release's version from its tag,
// whether a tree is clean, what a submodule records.

#include <QString>
#include <QStringList>

namespace git {

// git -C dir args...: its output, trimmed; fails with git's message.
QString out(const QString& dir, const QStringList& args);
// The same, but empty on failure.
QString tryOut(const QString& dir, const QStringList& args);
// Exit status only, output passed through.
int run(const QString& dir, const QStringList& args);

// The newest X.Y.Z of the tags `prefix`X.Y.Z (pi/v1.2.3) at HEAD; empty if none.
QString tagVersion(const QString& dir, const QString& prefix);
// The highest version among `tags` carrying `prefix`, without it ("1.10.0"
// over "1.9.2"); empty if none.
QString newestVersion(const QStringList& tags, const QString& prefix);
bool clean(const QString& dir);

} // namespace git
