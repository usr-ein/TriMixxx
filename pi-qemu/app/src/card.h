#pragma once
// Reading an SD card image the way the Pi's boot ROM does: the partition table
// here (two fixed structs), files off FAT partitions through mtools. Read-only,
// no mounting, no root, macOS and Linux.

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QVector>
#include <optional>

class Card {
public:
    struct Partition {
        int     number = 0; // 1-based, as config.txt and the kernel count them
        quint64 offset = 0; // bytes
        quint64 size   = 0;
        quint8  mbrType = 0;
    };

    bool open(const QString& path, QString* error);
    const QVector<Partition>& partitions() const { return m_parts; }
    qint64 size() const { return m_file.size(); }

    // A file off partition n's FAT filesystem ("overlays/disable-bt.dtbo").
    std::optional<QByteArray> readFile(int partition, const QString& path);
    // The first partition carrying a FAT filesystem: the boot ROM's fallback.
    int firstFat();

private:
    QByteArray readAt(quint64 offset, quint64 len);
    void readMbr();
    void readGpt();

    QFile              m_file;
    QVector<Partition> m_parts;
};
