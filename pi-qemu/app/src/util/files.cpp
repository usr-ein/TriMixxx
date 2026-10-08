#include "util/files.h"

#include "util/fail.h"
#include "util/process.h"

#include <QFile>

#ifdef __APPLE__
#include <sys/clonefile.h>
#endif

namespace files {

QByteArray read(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) fail("cannot read " + path + ": " + f.errorString());
    return f.readAll();
}

void write(const QString& path, const QByteArray& data, bool privateFile) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) fail("cannot write " + path + ": " + f.errorString());
    if (privateFile) f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (f.write(data) != data.size()) fail("cannot write " + path + ": " + f.errorString());
}

void clone(const QString& from, const QString& to) {
    QFile::remove(to);
#ifdef __APPLE__
    if (::clonefile(QFile::encodeName(from).constData(), QFile::encodeName(to).constData(), 0) == 0) return;
#endif
    if (!QFile::copy(from, to)) fail("cannot copy " + from + " to " + to);
}

bool inUse(const QString& path) {
    proc::Options o;
    o.quiet = true;
    return proc::capture("lsof", {path}, o).ok();
}

} // namespace files
