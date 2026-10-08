#pragma once
// A USB stick made up from a folder on this Mac: a FAT32 disk whose
// directories and FATs are built in memory when it is inserted, and whose
// files are read from the folder only when the Pi reads them. Nothing is
// copied and the folder is only ever read. NbdServer hands it to QEMU.
//
// It can follow a real stick's layout: an MBR or none at all (a
// "superfloppy", the filesystem from sector 0), the stick's size, and the very
// sectors its filesystem starts with -- boot code, BPB, serial, label, dirty
// flag -- taken from a raw copy, with the FATs, FSInfo and directories made to
// match. Names are stored NFC, as rekordbox's sticks have them: macOS hands a
// folder's accented names back decomposed, which no stick ever stored.

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

#include <memory>
#include <mutex>

struct FatSpec {
    QString    folder;                 // what is on the stick
    bool       superfloppy = false;    // the filesystem from sector 0, no partition table
    quint32    partitionStart = 2048;  // in sectors, under an MBR
    quint64    size = 0;               // the disk, in bytes; 0: what the files need, with room to spare
    QString    label;                  // the volume label; none when empty
    quint32    serial = 0;             // the volume serial, blkid's UUID; 0: one made from the folder's path
    quint32    clusterBytes = 0;       // 0: as mkfs.fat would choose for the size
    QByteArray volumeStart;            // a real volume's reserved sectors (boot sector, FSInfo, backup), as they were
    QByteArray mbr;                    // a real disk's first sector, partition table and all
    qint64     bytesPerSecond = 0;     // read no faster than this, like a slow stick (0: as fast as it goes)

    // A .stick file: JSON naming the folder and any of the above (README, "USB sticks").
    static FatSpec load(const QString& file); // throws Failure
};

class FatVolume {
public:
    // Walks the folder and lays the disk out. Throws Failure when the folder
    // cannot be a FAT32 stick: too big for the disk, a file of 4 GiB or more,
    // a volume start it cannot read.
    explicit FatVolume(const FatSpec& spec);
    ~FatVolume();

    quint64 size() const { return m_size; }
    // The disk's bytes at [offset, offset + length): thread-safe. False when
    // a file could not be read, with *error saying which.
    bool read(quint64 offset, char* out, quint64 length, QString* error) const;
    // "8128 files in 1406 folders, 76.2 GB on a 123.0 GB superfloppy"
    QString summary() const;

private:
    struct Entry;
    struct Run {
        quint32 first = 0, count = 0; // clusters
        int     entry = -1;
    };

    void walk(int dir, const QString& hostDir, int depth);
    void layOut(const FatSpec& spec, quint64 neededBytes);
    void allocate(int index);
    QByteArray directoryBytes(int dir) const;
    bool readData(quint64 rel, char* out, quint64 n, QString* error) const;
    int  fileFd(int entry, QString* error) const;

    QVector<Entry>   m_entries;      // [0] is the root
    QVector<Run>     m_runs;         // by first cluster
    QVector<quint32> m_fat;
    QByteArray       m_mbr;          // the first sector, under a partition table
    QByteArray       m_reserved;     // the volume's reserved sectors
    QString          m_label;
    bool             m_superfloppy = false;
    quint64 m_size = 0, m_volumeOffset = 0, m_volumeBytes = 0;
    quint64 m_fatBytes = 0, m_dataOffset = 0; // m_dataOffset from the volume's start
    quint32 m_nfats = 2, m_clusterBytes = 0, m_clusterCount = 0, m_nextCluster = 2;
    int     m_files = 0, m_dirs = 0;
    quint64 m_usedBytes = 0;

    mutable std::mutex        m_fdLock;
    mutable QVector<int>      m_fdOrder;  // entries with an open descriptor, oldest first
    mutable QHash<int, int>   m_fds;      // entry -> descriptor
};
