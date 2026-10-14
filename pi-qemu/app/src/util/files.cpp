#include "util/files.h"

#include "util/fail.h"
#include "util/process.h"

#include <QFile>
#include <QRegularExpression>

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

void unxz(const QString& from, const QString& to) {
    proc::Options o;
    o.quiet = true;
    // Its size, from the .xz's index: dd leaves trailing zeros out.
    const QStringList list = proc::capture("xz", {"--robot", "--list", from}, o).text().split('\n');
    const QStringList file = list.filter(QRegularExpression("^file\t"));
    const qint64 size = file.isEmpty() ? 0 : file.first().split('\t').value(4).toLongLong();
    if (size <= 0) fail("cannot read " + from + " (xz --list)");
    const QString part = to + ".part";
    QFile::remove(part);
    if (proc::run("bash", {"-c", "set -o pipefail; xz -dcT0 -- \"$1\" | dd of=\"$2\" bs=1m conv=sparse 2>/dev/null",
                           "unxz", from, part}) != 0 ||
        !QFile::resize(part, size)) {
        QFile::remove(part);
        fail("could not decompress " + from);
    }
    QFile::remove(to);
    if (!QFile::rename(part, to)) fail("cannot move " + part + " to " + to);
}

} // namespace files
