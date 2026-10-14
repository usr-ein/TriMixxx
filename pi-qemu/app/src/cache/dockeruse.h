#pragma once
// What Docker's caches hold, for `pi-qemu cache`: its build cache by record
// (Mixxx's build trees, one per checkout, the ccache, the cargo registry, the
// layers), its images, and its VM's free disk. Read only while the VM runs,
// since asking Docker anything wakes a VM Resource Saver has stopped; a
// stopped VM's caches can't change, so the last reading, kept in
// .cache/docker.json, stands for it, as old as it is.

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace cache {

struct DockerUse {
    qint64 at = 0;                 // when it was read, seconds since the epoch; 0: never
    bool   fresh = false;          // read just now, the VM up
    qint64 vmFree = -1;            // the VM's free disk, bytes; -1: unknown
    struct Mount { QString id; qint64 bytes = 0; };
    QVector<Mount> mounts;         // the build cache's cache mounts, by id ("/mixxx-build-debian:trixie")
    qint64 layers = 0, contexts = 0, otherRecords = 0, images = 0;
    int    records = 0;

    QJsonObject toJson() const;
    static DockerUse fromJson(const QJsonObject& o);
};

// Read now if Docker's VM runs (and kept in `keep`, unless empty); else what
// `keep` holds.
DockerUse dockerUse(const QString& keep);

// "556.6MB", "1.506GB", "0B", as docker prints them (decimal units), in bytes.
qint64 dockerBytes(const QString& size);

} // namespace cache
