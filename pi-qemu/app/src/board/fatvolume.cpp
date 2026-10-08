#include "board/fatvolume.h"

#include "util/fail.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static_assert(Q_BYTE_ORDER == Q_LITTLE_ENDIAN, "the FAT is served straight from memory, little-endian as on disk");

struct FatVolume::Entry {
    QString    name;        // NFC, as the stick stores it
    QByteArray hostPath;    // where its bytes are, for open()
    bool       dir = false, hidden = false;
    quint64    size = 0;
    qint64     mtime = 0, btime = 0;
    int        parent = -1;
    QVector<int> children;
    QByteArray shortName;   // the 11 bytes of its 8.3 name
    quint8     caseFlags = 0;
    int        lfnEntries = 0; // 0: the 8.3 name says it all
    quint32    first = 0, clusters = 0;
    QByteArray dirData;     // a folder's entries, without the zeros after them
};

namespace {

constexpr quint64 kSector = 512;
constexpr quint32 kEndOfChain = 0x0FFFFFFF;
constexpr int kMaxOpenFiles = 32;

void put16(char* p, quint16 v) { qToLittleEndian(v, p); }
void put32(char* p, quint32 v) { qToLittleEndian(v, p); }
quint16 get16(const QByteArray& b, int at) { return qFromLittleEndian<quint16>(b.constData() + at); }
quint32 get32(const QByteArray& b, int at) { return qFromLittleEndian<quint32>(b.constData() + at); }

// FAT's local date and time, clamped to the years it can hold.
QDateTime local(qint64 secs) {
    QDateTime t = QDateTime::fromSecsSinceEpoch(secs).toLocalTime();
    if (t.date().year() < 1980) return QDateTime(QDate(1980, 1, 1), QTime(0, 0));
    if (t.date().year() > 2107) return QDateTime(QDate(2107, 12, 31), QTime(23, 59, 58));
    return t;
}
quint16 dosDate(qint64 secs) {
    const QDate d = local(secs).date();
    return quint16(((d.year() - 1980) << 9) | (d.month() << 5) | d.day());
}
quint16 dosTime(qint64 secs) {
    const QTime t = local(secs).time();
    return quint16((t.hour() << 11) | (t.minute() << 5) | (t.second() / 2));
}

// What an 8.3 name may hold besides A-Z and 0-9. Anything else -- a space, a
// second period, a letter outside ASCII -- takes a long name.
bool shortChar(char16_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c && c < 0x80 && std::strchr("!#$%&'()-@^_`{}~", char(c)));
}

// A name that is an 8.3 name already keeps it, with no long name, as Windows
// stores it: "EXPORT.PDB" as it is, "a1.jpg" upper-cased with the two flags
// that say the base and the extension are lower case (Linux shows it lower).
bool exactShort(const QString& name, QByteArray* out, quint8* flags) {
    const int dot = name.lastIndexOf('.');
    if (name.count('.') > 1 || dot == 0) return false;
    const QString base = dot < 0 ? name : name.left(dot), ext = dot < 0 ? QString() : name.mid(dot + 1);
    if (base.isEmpty() || base.size() > 8 || ext.size() > 3 || (dot > 0 && ext.isEmpty())) return false;
    *flags = 0;
    auto part = [&](const QString& s, quint8 lowerFlag) {
        bool upper = false, lower = false;
        for (QChar c : s) {
            const char16_t u = c.unicode();
            if (u >= 'a' && u <= 'z') lower = true;
            else if (u >= 'A' && u <= 'Z') upper = true;
            else if (!shortChar(u)) return false;
        }
        if (upper && lower) return false;
        if (lower) *flags |= lowerFlag;
        return true;
    };
    if (!part(base, 0x08) || !part(ext, 0x10)) return false;
    *out = base.toUpper().toLatin1().leftJustified(8, ' ') + ext.toUpper().toLatin1().leftJustified(3, ' ');
    return true;
}

// Any other name gets a long name and a made-up 8.3 one: Windows' basis name
// (upper case, no spaces, no periods but the last, '_' for what 8.3 cannot
// hold) and the first free numeric tail, ~1, ~2, ...
QByteArray numberedShort(const QString& name, QSet<QByteArray>* taken) {
    const QString up = name.toUpper();
    const int dot = up.lastIndexOf('.');
    auto clean = [](const QString& s, int max) {
        QByteArray out;
        for (QChar c : s) {
            const char16_t u = c.unicode();
            if (u == ' ' || u == '.') continue;
            if (QChar::isLowSurrogate(u)) continue; // one '_' per character, not per half
            out += shortChar(u) ? char(u) : '_';
            if (out.size() == max) break;
        }
        return out;
    };
    QByteArray base = clean(dot > 0 ? up.left(dot) : up, 8);
    const QByteArray ext = clean(dot > 0 ? up.mid(dot + 1) : QString(), 3);
    if (base.isEmpty()) base = "_";
    for (int i = 1;; i++) {
        const QByteArray tail = "~" + QByteArray::number(i);
        const QByteArray candidate = (base.left(8 - tail.size()) + tail).leftJustified(8, ' ') + ext.leftJustified(3, ' ');
        if (!taken->contains(candidate)) {
            taken->insert(candidate);
            return candidate;
        }
    }
}

quint8 checksum(const QByteArray& shortName) {
    quint8 sum = 0;
    for (int i = 0; i < 11; i++) sum = quint8(((sum & 1) << 7) + (sum >> 1) + quint8(shortName[i]));
    return sum;
}

QByteArray shortEntry(const QByteArray& name, quint8 attr, quint8 caseFlags, quint32 cluster, quint32 size,
                      qint64 mtime, qint64 btime) {
    QByteArray e(32, 0);
    char* p = e.data();
    std::memcpy(p, name.constData(), 11);
    if (quint8(p[0]) == 0xE5) p[0] = 0x05; // 0xE5 there would mean deleted
    p[11] = char(attr);
    p[12] = char(caseFlags);
    put16(p + 14, dosTime(btime));
    put16(p + 16, dosDate(btime));
    put16(p + 18, dosDate(mtime));
    put16(p + 20, quint16(cluster >> 16));
    put16(p + 22, dosTime(mtime));
    put16(p + 24, dosDate(mtime));
    put16(p + 26, quint16(cluster & 0xFFFF));
    put32(p + 28, size);
    return e;
}

// A long name's entries, last part first, as they precede the 8.3 entry.
QByteArray lfnEntries(const QString& name, quint8 sum) {
    const int n = int((name.size() + 12) / 13);
    QByteArray out;
    for (int seq = n; seq >= 1; seq--) {
        QByteArray e(32, 0);
        char* p = e.data();
        p[0] = char(seq | (seq == n ? 0x40 : 0));
        p[11] = 0x0F;
        p[13] = char(sum);
        static const int at[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
        for (int k = 0; k < 13; k++) {
            const int i = (seq - 1) * 13 + k;
            // The name, a NUL after it when there is room, then 0xFFFF.
            const quint16 u = i < name.size() ? name[i].unicode() : i == name.size() ? 0x0000 : 0xFFFF;
            put16(p + at[k], u);
        }
        out += e;
    }
    return out;
}

QByteArray readAt(const QString& file, qint64 offset, qint64 length) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) fail(file + ": " + f.errorString());
    if (!f.seek(offset)) fail(file + ": cannot seek to " + QString::number(offset));
    const QByteArray b = f.read(length);
    if (b.size() != length) fail(QString("%1: %2 bytes at %3, but only %4 there").arg(file).arg(length).arg(offset).arg(b.size()));
    return b;
}

QString gb(quint64 bytes) { return QString::number(double(bytes) / 1e9, 'f', 1) + " GB"; }

} // namespace

FatSpec FatSpec::load(const QString& file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) fail(file + ": " + f.errorString());
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (!doc.isObject()) fail(file + ": " + perr.errorString());
    const QJsonObject o = doc.object();
    const QDir base = QFileInfo(file).absoluteDir();
    auto path = [&](const QString& p) { return QDir::isAbsolutePath(p) ? p : base.filePath(p); };
    // {"file": F, "offset": N}, or just F for its start.
    auto source = [&](const char* key, QString* where, qint64* offset) {
        const QJsonValue v = o.value(key);
        *where = path(v.isString() ? v.toString() : v.toObject().value("file").toString());
        *offset = v.isObject() ? qint64(v.toObject().value("offset").toDouble()) : 0;
    };

    FatSpec s;
    if (o.value("folder").toString().isEmpty()) fail(file + ": no \"folder\"");
    s.folder = path(o.value("folder").toString());
    s.superfloppy = o.value("superfloppy").toBool();
    s.partitionStart = quint32(o.value("partitionStart").toInt(2048));
    s.size = quint64(o.value("size").toDouble());
    s.label = o.value("label").toString();
    s.clusterBytes = quint32(o.value("clusterKiB").toInt() * 1024);
    s.bytesPerSecond = qint64(o.value("bytesPerSecond").toDouble());
    s.readsPerSecond = o.value("readsPerSecond").toInt();
    if (o.contains("serial")) {
        bool ok = false;
        s.serial = o.value("serial").toString().remove('-').toUInt(&ok, 16);
        if (!ok) fail(file + ": \"serial\" is not like E02C-5D1B");
    }
    if (o.contains("volumeStart")) {
        QString where;
        qint64 offset;
        source("volumeStart", &where, &offset);
        const QByteArray boot = readAt(where, offset, kSector);
        const int reserved = get16(boot, 14);
        if (reserved < 2) fail(where + ": no FAT32 boot sector at byte " + QString::number(offset));
        s.volumeStart = readAt(where, offset, reserved * qint64(kSector));
    }
    if (o.contains("mbr")) {
        QString where;
        qint64 offset;
        source("mbr", &where, &offset);
        s.mbr = readAt(where, offset, kSector);
    }
    return s;
}

FatVolume::FatVolume(const FatSpec& spec) {
    if (!QFileInfo(spec.folder).isDir()) fail(spec.folder + ": not a folder");
    m_superfloppy = spec.superfloppy;
    m_label = spec.label;

    Entry root;
    root.dir = true;
    root.hostPath = QFile::encodeName(spec.folder);
    m_entries.append(root);
    walk(0, spec.folder, 0);

    // 8.3 names folder by folder. Those that are 8.3 already go first, so a
    // made-up one never takes theirs.
    quint64 needed = 0;
    for (int d = 0; d < m_entries.size(); d++) {
        if (!m_entries[d].dir) continue;
        QSet<QByteArray> taken;
        for (int c : m_entries[d].children)
            if (exactShort(m_entries[c].name, &m_entries[c].shortName, &m_entries[c].caseFlags))
                taken.insert(m_entries[c].shortName);
        for (int c : m_entries[d].children) {
            Entry& e = m_entries[c];
            if (!e.shortName.isEmpty()) continue;
            e.shortName = numberedShort(e.name, &taken);
            e.lfnEntries = int((e.name.size() + 12) / 13);
        }
        needed += 64 * 1024; // its folder, generously
    }
    for (const Entry& e : m_entries)
        if (!e.dir) needed += (e.size + 32767) / 32768 * 32768;

    layOut(spec, needed);
    allocate(0);
    for (int d = 0; d < m_entries.size(); d++)
        if (m_entries[d].dir) m_entries[d].dirData = directoryBytes(d);
    std::sort(m_runs.begin(), m_runs.end(), [](const Run& a, const Run& b) { return a.first < b.first; });

    // FSInfo, here and in the backup: the free count and where to look next,
    // as a FAT32 driver expects them to be.
    const quint32 freeClusters = m_clusterCount - (m_nextCluster - 2);
    const quint16 fsinfo = get16(m_reserved, 48), backup = get16(m_reserved, 50);
    for (quint32 sector : {quint32(fsinfo), quint32(backup) + fsinfo}) {
        if (sector == 0 || sector == 0xFFFF || (sector + 1) * kSector > quint64(m_reserved.size())) continue;
        char* p = m_reserved.data() + sector * kSector;
        std::memset(p, 0, kSector);
        put32(p, 0x41615252);
        put32(p + 484, 0x61417272);
        put32(p + 488, freeClusters);
        put32(p + 492, m_nextCluster);
        put32(p + 508, 0xAA550000);
    }
}

FatVolume::~FatVolume() {
    for (int fd : std::as_const(m_fds)) ::close(fd);
}

void FatVolume::walk(int dir, const QString& hostDir, int depth) {
    if (depth > 48) fail(hostDir + ": folders 48 deep; a link that loops?");
    const QFileInfoList list = QDir(hostDir).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Unsorted);
    QSet<QString> seen; // FAT's names are case-insensitive
    for (const QFileInfo& fi : list) {
        Entry e;
        e.name = fi.fileName().normalized(QString::NormalizationForm_C);
        e.hostPath = QFile::encodeName(fi.absoluteFilePath());
        struct stat st;
        if (::stat(e.hostPath.constData(), &st) != 0) continue; // a link to nothing
        e.dir = S_ISDIR(st.st_mode);
        if (!e.dir && !S_ISREG(st.st_mode)) continue;
        if (e.name.size() > 255) fail(fi.absoluteFilePath() + ": a name longer than FAT's 255 characters");
        if (!e.dir && quint64(st.st_size) > 0xFFFFFFFFull)
            fail(fi.absoluteFilePath() + ": 4 GiB or more, which no FAT32 stick can hold");
        const QString folded = e.name.toCaseFolded();
        if (seen.contains(folded)) fail(fi.absoluteFilePath() + ": two names that differ only in case");
        seen.insert(folded);
        e.size = e.dir ? 0 : quint64(st.st_size);
        e.mtime = st.st_mtimespec.tv_sec;
        e.btime = st.st_birthtimespec.tv_sec;
        e.hidden = st.st_flags & UF_HIDDEN;
        e.parent = dir;
        const int index = int(m_entries.size());
        m_entries.append(e);
        m_entries[dir].children.append(index);
        if (e.dir) {
            m_dirs++;
            walk(index, fi.absoluteFilePath(), depth + 1);
        } else {
            m_files++;
            m_usedBytes += e.size;
        }
    }
}

void FatVolume::layOut(const FatSpec& spec, quint64 needed) {
    quint32 reserved, fatSectors, totalSectors, spc;
    if (!spec.mbr.isEmpty()) {
        // A real disk's partition table: the volume is where its first partition starts.
        m_mbr = spec.mbr;
        m_superfloppy = false;
        m_volumeOffset = quint64(get32(m_mbr, 446 + 8)) * kSector;
        if (m_volumeOffset == 0) fail("the MBR given has no first partition");
    } else {
        m_volumeOffset = m_superfloppy ? 0 : quint64(spec.partitionStart) * kSector;
    }

    if (!spec.volumeStart.isEmpty()) {
        // A real volume's start, served as it was: geometry, serial, label,
        // boot code, flags. Only the FSInfo sectors are made to match.
        m_reserved = spec.volumeStart;
        if (get16(m_reserved, 11) != kSector) fail("the volume start given is not of 512-byte sectors");
        if (get16(m_reserved, 22) != 0) fail("the volume start given is not FAT32");
        spc = quint8(m_reserved[13]);
        reserved = get16(m_reserved, 14);
        m_nfats = quint8(m_reserved[16]);
        totalSectors = get16(m_reserved, 19) ? get16(m_reserved, 19) : get32(m_reserved, 32);
        fatSectors = get32(m_reserved, 36);
        m_nextCluster = get32(m_reserved, 44); // the root folder's cluster
        if (spc == 0 || (spc & (spc - 1)) || m_nfats == 0 || m_nextCluster < 2)
            fail("the volume start given is not a FAT32 boot sector");
    } else {
        // As mkfs.fat lays one out: 32 reserved sectors, two FATs, and the
        // cluster size Microsoft's table gives for the size.
        quint64 disk = spec.size ? spec.size
                                 : std::max<quint64>(1ull << 30, (needed + needed / 4 + (1 << 20)) & ~quint64((1 << 20) - 1)) +
                                           m_volumeOffset;
        if (disk <= m_volumeOffset + (64ull << 20)) fail("a disk that small cannot hold FAT32");
        totalSectors = quint32(std::min<quint64>((disk - m_volumeOffset) / kSector, 0xFFFFFFFFull));
        const quint64 vol = quint64(totalSectors) * kSector;
        quint32 cluster = spec.clusterBytes ? spec.clusterBytes
                          : vol <= (8ull << 30)  ? 4096
                          : vol <= (16ull << 30) ? 8192
                          : vol <= (32ull << 30) ? 16384
                                                 : 32768;
        spc = cluster / kSector;
        reserved = 32;
        m_nfats = 2;
        fatSectors = 0;
        for (;;) {
            const quint64 clusters = (totalSectors - reserved - m_nfats * fatSectors) / spc;
            const quint32 need = quint32(((clusters + 2) * 4 + kSector - 1) / kSector);
            if (need <= fatSectors) break;
            fatSectors = need;
        }
        const quint32 serial = spec.serial ? spec.serial : quint32(qHash(spec.folder, 0x7a17));
        m_reserved = QByteArray(int(reserved * kSector), 0);
        char* b = m_reserved.data();
        std::memcpy(b, "\xEB\x58\x90mkfs.fat", 11);
        put16(b + 11, kSector);
        b[13] = char(spc);
        put16(b + 14, quint16(reserved));
        b[16] = char(m_nfats);
        b[21] = char(0xF8);
        put16(b + 24, 63);
        put16(b + 26, 255);
        put32(b + 28, quint32(m_volumeOffset / kSector));
        put32(b + 32, totalSectors);
        put32(b + 36, fatSectors);
        put32(b + 44, 2);
        put16(b + 48, 1);
        put16(b + 50, 6);
        b[64] = char(0x80);
        b[66] = 0x29;
        put32(b + 67, serial);
        const QByteArray label = (spec.label.isEmpty() ? QByteArray("NO NAME") : spec.label.toUpper().toLatin1()).left(11);
        std::memcpy(b + 71, label.leftJustified(11, ' ').constData(), 11);
        std::memcpy(b + 82, "FAT32   ", 8);
        b[510] = 0x55;
        b[511] = char(0xAA);
        std::memcpy(b + 6 * kSector, b, kSector); // the backup boot sector
        m_nextCluster = 2;
    }
    if (!m_superfloppy && m_mbr.isEmpty()) {
        // One partition, FAT32 (LBA), from partitionStart to the volume's end.
        m_mbr = QByteArray(int(kSector), 0);
        char* m = m_mbr.data();
        put32(m + 440, get32(m_reserved, 67) ^ 0x5a17c0deu); // the disk's signature
        const char chs[3] = {char(0xFE), char(0xFF), char(0xFF)}; // past what CHS can say: use the LBA
        std::memcpy(m + 446 + 1, chs, 3);
        m[446 + 4] = 0x0C;
        std::memcpy(m + 446 + 5, chs, 3);
        put32(m + 446 + 8, quint32(m_volumeOffset / kSector));
        put32(m + 446 + 12, totalSectors);
        m[510] = 0x55;
        m[511] = char(0xAA);
    }

    m_clusterBytes = spc * kSector;
    m_volumeBytes = quint64(totalSectors) * kSector;
    m_fatBytes = quint64(fatSectors) * kSector;
    m_dataOffset = quint64(reserved) * kSector + m_nfats * m_fatBytes;
    if (m_dataOffset >= m_volumeBytes) fail("the volume start given leaves no room for data");
    m_clusterCount = quint32(std::min<quint64>((m_volumeBytes - m_dataOffset) / m_clusterBytes, m_fatBytes / 4 - 2));
    m_size = std::max(spec.size, m_volumeOffset + m_volumeBytes);
    m_fat = QVector<quint32>(int(m_clusterCount) + 2, 0);
    m_fat[0] = 0x0FFFFF00 | quint8(m_reserved[21]);
    m_fat[1] = kEndOfChain;
}

// Clusters in the order a copy onto an empty stick would take them: a folder,
// then what is in it, one after the other, each file in one piece.
void FatVolume::allocate(int index) {
    Entry& e = m_entries[index];
    if (e.dir) {
        int count = index == 0 ? (m_label.isEmpty() ? 0 : 1) : 2;
        for (int c : e.children) count += 1 + m_entries[c].lfnEntries;
        if (count > 65536) fail(QString::fromUtf8(e.hostPath) + ": more names than a FAT folder can hold");
        e.clusters = std::max<quint32>(1, quint32((quint64(count) * 32 + m_clusterBytes - 1) / m_clusterBytes));
    } else {
        e.clusters = quint32((e.size + m_clusterBytes - 1) / m_clusterBytes);
    }
    if (e.clusters) {
        if (quint64(m_nextCluster) - 2 + e.clusters > m_clusterCount)
            fail(QString("%1 does not fit on a %2 stick").arg(QString::fromUtf8(m_entries[0].hostPath), gb(m_size)));
        e.first = m_nextCluster;
        for (quint32 c = e.first; c < e.first + e.clusters - 1; c++) m_fat[int(c)] = c + 1;
        m_fat[int(e.first + e.clusters - 1)] = kEndOfChain;
        m_nextCluster += e.clusters;
        m_runs.append({e.first, e.clusters, index});
    }
    if (e.dir)
        for (int c : std::as_const(m_entries[index].children)) allocate(c);
}

QByteArray FatVolume::directoryBytes(int d) const {
    const Entry& e = m_entries[d];
    QByteArray out;
    if (d == 0) {
        if (!m_label.isEmpty())
            out += shortEntry(m_label.toUpper().toLatin1().left(11).leftJustified(11, ' '), 0x08, 0, 0, 0, e.mtime, e.mtime);
    } else {
        // ".." of a folder at the top says cluster 0, not the root's.
        const quint32 up = e.parent == 0 ? 0 : m_entries[e.parent].first;
        out += shortEntry(".          ", 0x10, 0, e.first, 0, e.mtime, e.btime);
        out += shortEntry("..         ", 0x10, 0, up, 0, e.mtime, e.btime);
    }
    for (int c : e.children) {
        const Entry& k = m_entries[c];
        if (k.lfnEntries) out += lfnEntries(k.name, checksum(k.shortName));
        const quint8 attr = quint8((k.dir ? 0x10 : 0x20) | (k.hidden ? 0x02 : 0));
        out += shortEntry(k.shortName, attr, k.caseFlags, k.first, quint32(k.size), k.mtime, k.btime);
    }
    return out;
}

int FatVolume::fileFd(int entry, QString* error) const {
    if (auto it = m_fds.constFind(entry); it != m_fds.constEnd()) {
        m_fdOrder.removeOne(entry);
        m_fdOrder.append(entry);
        return *it;
    }
    const int fd = ::open(m_entries[entry].hostPath.constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        *error = QString("%1: %2").arg(QString::fromUtf8(m_entries[entry].hostPath), QString::fromLocal8Bit(strerror(errno)));
        return -1;
    }
    if (m_fdOrder.size() >= kMaxOpenFiles) ::close(m_fds.take(m_fdOrder.takeFirst()));
    m_fds.insert(entry, fd);
    m_fdOrder.append(entry);
    return fd;
}

bool FatVolume::readData(quint64 rel, char* out, quint64 n, QString* error) const {
    while (n > 0) {
        const quint32 cluster = quint32(rel / m_clusterBytes) + 2;
        // The run holding the cluster: the last one starting at or before it.
        auto it = std::upper_bound(m_runs.begin(), m_runs.end(), cluster,
                                   [](quint32 c, const Run& r) { return c < r.first; });
        quint64 chunk;
        if (it == m_runs.begin() || cluster >= (it - 1)->first + (it - 1)->count) {
            const quint64 next = it == m_runs.end() ? ~0ull : quint64(it->first - 2) * m_clusterBytes;
            chunk = std::min(n, next - rel); // free clusters, up to the next used one
            std::memset(out, 0, chunk);
        } else {
            const Run& r = *(it - 1);
            const Entry& e = m_entries[r.entry];
            const quint64 within = rel - quint64(r.first - 2) * m_clusterBytes;
            chunk = std::min(n, quint64(r.count) * m_clusterBytes - within);
            const quint64 have = e.dir ? (within < quint64(e.dirData.size()) ? std::min<quint64>(chunk, e.dirData.size() - within) : 0)
                                       : (within < e.size ? std::min(chunk, e.size - within) : 0);
            if (e.dir) {
                std::memcpy(out, e.dirData.constData() + within, have);
            } else if (have) {
                std::lock_guard<std::mutex> lock(m_fdLock);
                // Twice at most: a descriptor that fails is dropped and the
                // file opened again by its path, so a drive that went away
                // for a moment (a cable knocked out and back) does not leave
                // the stick failing for good on descriptors to its old mount.
                quint64 got = 0;
                int failures = 0;
                while (got < have) {
                    const int fd = fileFd(r.entry, error);
                    if (fd < 0) return false;
                    const ssize_t k = ::pread(fd, out + got, have - got, off_t(within + got));
                    if (k < 0 && errno == EINTR) continue;
                    if (k > 0) {
                        got += quint64(k);
                        continue;
                    }
                    *error = QString("%1: %2").arg(QString::fromUtf8(e.hostPath),
                                                   k < 0 ? QString::fromLocal8Bit(strerror(errno)) : "shorter than it was");
                    m_fdOrder.removeOne(r.entry);
                    ::close(m_fds.take(r.entry));
                    if (++failures > 1) return false;
                }
            }
            std::memset(out + have, 0, chunk - have); // the rest of its last cluster
        }
        out += chunk;
        rel += chunk;
        n -= chunk;
    }
    return true;
}

bool FatVolume::read(quint64 offset, char* out, quint64 length, QString* error) const {
    const quint64 volume = m_volumeOffset, fats = volume + quint64(m_reserved.size()),
                  data = volume + m_dataOffset, end = volume + m_volumeBytes;
    while (length > 0) {
        quint64 n = length;
        if (offset < volume) { // the MBR, then nothing up to the partition
            n = std::min(n, volume - offset);
            std::memset(out, 0, n);
            if (offset < kSector && !m_mbr.isEmpty())
                std::memcpy(out, m_mbr.constData() + offset, std::min(n, kSector - offset));
        } else if (offset < fats) {
            n = std::min(n, fats - offset);
            std::memcpy(out, m_reserved.constData() + (offset - volume), n);
        } else if (offset < data) {
            const quint64 within = (offset - fats) % m_fatBytes; // every FAT the same
            n = std::min(n, m_fatBytes - within);
            const quint64 have = quint64(m_fat.size()) * 4, k = within < have ? std::min(n, have - within) : 0;
            std::memcpy(out, reinterpret_cast<const char*>(m_fat.constData()) + within, k);
            std::memset(out + k, 0, n - k);
        } else if (offset < end) {
            n = std::min(n, end - offset);
            if (!readData(offset - data, out, n, error)) return false;
        } else {
            std::memset(out, 0, n);
        }
        out += n;
        offset += n;
        length -= n;
    }
    return true;
}

QString FatVolume::pathOf(int entry) const {
    QStringList parts;
    for (int e = entry; e > 0; e = m_entries[e].parent) parts.prepend(m_entries[e].name);
    return "/" + parts.join('/');
}

QString FatVolume::describe(quint64 offset) const {
    const quint64 volume = m_volumeOffset, fats = volume + quint64(m_reserved.size()),
                  data = volume + m_dataOffset, end = volume + m_volumeBytes;
    if (offset < volume) return offset < kSector ? "partition table" : "before the partition";
    if (offset < fats) return "boot sectors";
    if (offset < data) return "FAT";
    if (offset >= end) return "past the volume";
    const quint32 cluster = quint32((offset - data) / m_clusterBytes) + 2;
    auto it = std::upper_bound(m_runs.begin(), m_runs.end(), cluster,
                               [](quint32 c, const Run& r) { return c < r.first; });
    if (it == m_runs.begin() || cluster >= (it - 1)->first + (it - 1)->count) return "free space";
    const int entry = (it - 1)->entry;
    return m_entries[entry].dir ? "folder " + (entry == 0 ? QString("/") : pathOf(entry) + "/") : pathOf(entry);
}

QString FatVolume::summary() const {
    return QString("%1 files in %2 folders, %3 on a %4 %5")
        .arg(m_files)
        .arg(m_dirs)
        .arg(gb(m_usedBytes), gb(m_size),
             m_superfloppy ? QString("superfloppy") : QString("stick (MBR, FAT32 from sector %1)").arg(m_volumeOffset / kSector));
}
