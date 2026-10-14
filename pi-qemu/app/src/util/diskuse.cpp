#include "util/diskuse.h"

#include <QFile>
#include <QList>

#include <cstring>
#include <vector>
#include <fcntl.h>
#include <sys/attr.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <unistd.h>

namespace diskuse {

Use& Use::operator+=(const Use& o) {
    files += o.files;
    apparent += o.apparent;
    allocated += o.allocated;
    own += o.own;
    return *this;
}

namespace {

// What getattrlist(bulk) is asked for. FSOPT_PACK_INVAL_ATTRS gives every
// record the same layout, an attribute the object hasn't (a directory's
// sizes, a private size off APFS) zeroed and its bit cleared:
//   u_int32_t length; attribute_set_t returned; attrreference_t name;
//   fsobj_type_t type; off_t datalength, dataallocsize; off_t privatesize
attrlist request() {
    attrlist a{};
    a.bitmapcount = ATTR_BIT_MAP_COUNT;
    a.commonattr = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_NAME | ATTR_CMN_OBJTYPE;
    a.fileattr = ATTR_FILE_DATALENGTH | ATTR_FILE_DATAALLOCSIZE;
    a.forkattr = ATTR_CMNEXT_PRIVATESIZE; // with FSOPT_ATTR_CMN_EXTENDED: an extended common attribute
    return a;
}
constexpr unsigned long kOptions = FSOPT_ATTR_CMN_EXTENDED | FSOPT_PACK_INVAL_ATTRS | FSOPT_NOFOLLOW;

struct Entry {
    QByteArray name;
    fsobj_type_t type = VNON;
    Use use;
};

template <typename T> T at(const char* p) {
    T v;
    std::memcpy(&v, p, sizeof v); // the buffer's fields are 4-byte aligned only
    return v;
}

// One record; its length, to step to the next.
quint32 parse(const char* record, Entry* e) {
    const quint32 length = at<quint32>(record);
    const char* p = record + sizeof(quint32);
    const auto returned = at<attribute_set_t>(p);
    p += sizeof(attribute_set_t);
    const auto name = at<attrreference_t>(p);
    if (returned.commonattr & ATTR_CMN_NAME)
        e->name = QByteArray(p + name.attr_dataoffset, int(name.attr_length > 0 ? name.attr_length - 1 : 0));
    p += sizeof(attrreference_t);
    e->type = returned.commonattr & ATTR_CMN_OBJTYPE ? at<fsobj_type_t>(p) : VNON;
    p += sizeof(fsobj_type_t);
    e->use = {};
    if (e->type == VREG) {
        e->use.files = 1;
        e->use.apparent = at<off_t>(p);
        e->use.allocated = at<off_t>(p + sizeof(off_t));
        // Off APFS no private size: all of it is its own.
        e->use.own = returned.forkattr & ATTR_CMNEXT_PRIVATESIZE ? at<off_t>(p + 2 * sizeof(off_t)) : e->use.allocated;
    }
    return length;
}

// A directory's entries, a batch at a time; its directories after.
Use walk(int dir) {
    Use total;
    attrlist a = request();
    std::vector<char> buf(64 * 1024);
    QList<QByteArray> dirs;
    for (;;) {
        const int n = ::getattrlistbulk(dir, &a, buf.data(), buf.size(), kOptions);
        if (n <= 0) break;
        const char* p = buf.data();
        for (int i = 0; i < n; i++) {
            Entry e;
            p += parse(p, &e);
            if (e.type == VREG) total += e.use;
            else if (e.type == VDIR) dirs << e.name;
        }
    }
    for (const QByteArray& name : dirs) {
        const int sub = ::openat(dir, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (sub < 0) continue;
        total += walk(sub);
        ::close(sub);
    }
    return total;
}

} // namespace

Use of(const QString& path) {
    const QByteArray p = QFile::encodeName(path);
    attrlist a = request();
    alignas(8) char buf[1024];
    if (::getattrlist(p.constData(), &a, buf, sizeof buf, kOptions) != 0) return {};
    Entry e;
    parse(buf, &e);
    if (e.type == VREG) return e.use;
    if (e.type != VDIR) return {};
    const int dir = ::open(p.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (dir < 0) return {};
    const Use u = walk(dir);
    ::close(dir);
    return u;
}

qint64 freeBytes(const QString& path) {
    struct statfs s;
    if (::statfs(QFile::encodeName(path).constData(), &s) != 0) return -1;
    return qint64(s.f_bavail) * qint64(s.f_bsize);
}

QString text(qint64 bytes) {
    if (bytes >= 1000LL * 1000 * 1000) return QString::number(bytes / 1e9, 'f', 1) + " GB";
    if (bytes >= 1000LL * 1000) return QString::number(qRound64(bytes / 1e6)) + " MB";
    if (bytes >= 1000) return QString::number(qRound64(bytes / 1e3)) + " KB";
    return QString::number(bytes) + " B";
}

} // namespace diskuse
