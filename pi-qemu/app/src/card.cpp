#include "card.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QtEndian>

namespace {

quint32 le32(const QByteArray& b, int o) { return qFromLittleEndian<quint32>(b.constData() + o); }
quint64 le64(const QByteArray& b, int o) { return qFromLittleEndian<quint64>(b.constData() + o); }

} // namespace

bool Card::open(const QString& path, QString* error) {
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        *error = m_file.errorString();
        return false;
    }
    QByteArray mbr = readAt(0, 512);
    if (mbr.size() < 512 || quint8(mbr[510]) != 0x55 || quint8(mbr[511]) != 0xAA) {
        *error = path + " has no partition table";
        return false;
    }
    if (quint8(mbr[446 + 4]) == 0xEE) readGpt(); else readMbr();
    if (m_parts.isEmpty()) {
        *error = path + " has no partitions";
        return false;
    }
    return true;
}

QByteArray Card::readAt(quint64 offset, quint64 len) {
    if (!m_file.seek(qint64(offset))) return {};
    return m_file.read(qint64(len));
}

void Card::readMbr() {
    QByteArray mbr = readAt(0, 512);
    for (int i = 0; i < 4; i++) {
        int o = 446 + i * 16;
        quint8 type = quint8(mbr[o + 4]);
        quint32 lba = le32(mbr, o + 8), count = le32(mbr, o + 12);
        if (!type || !count) continue;
        if (type == 0x05 || type == 0x0F || type == 0x85) {
            // Extended partition: a chain of EBRs, logical partitions from 5.
            quint32 ebr = lba;
            for (int n = 5; n < 64 && ebr; n++) {
                QByteArray b = readAt(quint64(ebr) * 512, 512);
                if (b.size() < 512 || quint8(b[510]) != 0x55) break;
                quint8 t = quint8(b[446 + 4]);
                quint32 rel = le32(b, 446 + 8), cnt = le32(b, 446 + 12);
                if (t && cnt) m_parts.push_back({n, (quint64(ebr) + rel) * 512, quint64(cnt) * 512, t});
                quint32 nextRel = le32(b, 462 + 8);
                ebr = nextRel ? lba + nextRel : 0;
            }
            continue;
        }
        m_parts.push_back({i + 1, quint64(lba) * 512, quint64(count) * 512, type});
    }
}

void Card::readGpt() {
    QByteArray h = readAt(512, 512);
    if (!h.startsWith("EFI PART")) return;
    quint64 entries = le64(h, 72);
    quint32 count = le32(h, 80), esize = le32(h, 84);
    QByteArray t = readAt(entries * 512, quint64(count) * esize);
    for (quint32 i = 0; i < count && (i + 1) * esize <= quint32(t.size()); i++) {
        int o = int(i * esize);
        quint64 first = le64(t, o + 32), last = le64(t, o + 40);
        if (!first) continue;
        m_parts.push_back({int(i + 1), first * 512, (last - first + 1) * 512, 0});
    }
}

// The filesystem itself is mtools' job: `mtype -i card.img@@offset ::/path`
// reads a file out of the FAT partition starting at that byte offset.
std::optional<QByteArray> Card::readFile(int partition, const QString& path) {
    for (const auto& p : m_parts) {
        if (p.number != partition) continue;
        QProcess mtype;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("MTOOLS_SKIP_CHECK", "1"); // images are not floppies
        mtype.setProcessEnvironment(env);
        mtype.start("mtype", {"-i", QString("%1@@%2").arg(m_file.fileName()).arg(p.offset),
                              "::/" + path});
        if (!mtype.waitForFinished(30000) || mtype.exitStatus() != QProcess::NormalExit ||
            mtype.exitCode() != 0)
            return std::nullopt;
        return mtype.readAllStandardOutput();
    }
    return std::nullopt;
}

bool Card::exists(int partition, const QString& path) {
    for (const auto& p : m_parts) {
        if (p.number != partition) continue;
        QProcess mdir;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("MTOOLS_SKIP_CHECK", "1");
        mdir.setProcessEnvironment(env);
        mdir.start("mdir", {"-b", "-i", QString("%1@@%2").arg(m_file.fileName()).arg(p.offset), "::/" + path});
        return mdir.waitForFinished(30000) && mdir.exitStatus() == QProcess::NormalExit && mdir.exitCode() == 0;
    }
    return false;
}

// The boot ROM's fallback: the first partition mtools can read as FAT.
int Card::firstFat() {
    for (const auto& p : m_parts) {
        QProcess minfo;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("MTOOLS_SKIP_CHECK", "1");
        minfo.setProcessEnvironment(env);
        minfo.start("minfo", {"-i", QString("%1@@%2").arg(m_file.fileName()).arg(p.offset), "::"});
        if (minfo.waitForFinished(30000) && minfo.exitCode() == 0) return p.number;
    }
    return 0;
}
