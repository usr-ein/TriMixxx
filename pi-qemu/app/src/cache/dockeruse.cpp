#include "cache/dockeruse.h"

#include "util/docker.h"
#include "util/files.h"
#include "util/process.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <cstdio>
#include <unistd.h>

namespace cache {

qint64 dockerBytes(const QString& size) {
    static const QRegularExpression re(R"(^\s*([0-9.]+)\s*([kKMGTP]?)i?B\s*$)");
    const auto m = re.match(size);
    if (!m.hasMatch()) return 0;
    static const QString units = "kMGTP";
    const int power = m.captured(2).isEmpty() ? 0 : int(units.indexOf(m.captured(2), 0, Qt::CaseInsensitive)) + 1;
    double bytes = m.captured(1).toDouble();
    for (int i = 0; i < power; i++) bytes *= 1000;
    return qint64(bytes);
}

QJsonObject DockerUse::toJson() const {
    QJsonArray m;
    for (const Mount& x : mounts) m.append(QJsonObject{{"id", x.id}, {"bytes", x.bytes}});
    return {{"at", at},         {"vmFree", vmFree}, {"mounts", m},       {"layers", layers},
            {"contexts", contexts}, {"other", otherRecords}, {"images", images}, {"records", records}};
}

DockerUse DockerUse::fromJson(const QJsonObject& o) {
    DockerUse d;
    d.at = o.value("at").toInteger();
    d.vmFree = o.value("vmFree").toInteger(-1);
    for (const QJsonValue& v : o.value("mounts").toArray())
        d.mounts.append({v.toObject().value("id").toString(), v.toObject().value("bytes").toInteger()});
    d.layers = o.value("layers").toInteger();
    d.contexts = o.value("contexts").toInteger();
    d.otherRecords = o.value("other").toInteger();
    d.images = o.value("images").toInteger();
    d.records = o.value("records").toInt();
    return d;
}

namespace {

// Each JSON line of a docker command's --format json output.
QVector<QJsonObject> lines(const QStringList& args) {
    proc::Options o;
    o.quiet = true;
    o.timeoutMs = 60000;
    const proc::Result r = proc::capture("docker", args, o);
    QVector<QJsonObject> out;
    if (!r.ok()) return out;
    for (const QByteArray& line : r.out.split('\n'))
        if (const QJsonDocument d = QJsonDocument::fromJson(line); d.isObject()) out << d.object();
    return out;
}

} // namespace

DockerUse dockerUse(const QString& keep) {
    DockerUse d;
    if (!docker::vmUp()) {
        if (!keep.isEmpty() && QFileInfo::exists(keep))
            d = DockerUse::fromJson(QJsonDocument::fromJson(files::read(keep)).object());
        return d;
    }
    // A build cache record is a layer (regular), a build's context
    // (source.local), or a cache mount: those name their id last, as
    // `... with id "/mixxx-build-debian:trixie-wt-x-1234abcd"`.
    static const QRegularExpression withId(R"re(with id "([^"]*)"\s*$)re");
    const QVector<QJsonObject> records = lines({"buildx", "du", "--format", "json"});
    if (records.isEmpty()) return d; // Docker not answering: nothing read, nothing kept
    for (const QJsonObject& r : records) {
        const qint64 bytes = dockerBytes(r.value("Size").toString());
        const QString type = r.value("Type").toString();
        d.records++;
        if (type == "exec.cachemount") {
            const QString id = withId.match(r.value("Description").toString()).captured(1);
            auto same = std::find_if(d.mounts.begin(), d.mounts.end(), [&](const DockerUse::Mount& m) { return m.id == id; });
            if (same != d.mounts.end()) same->bytes += bytes;
            else d.mounts.append({id, bytes});
        } else if (type == "regular") {
            d.layers += bytes;
        } else if (type == "source.local") {
            d.contexts += bytes;
        } else {
            d.otherRecords += bytes;
        }
    }
    for (const QJsonObject& t : lines({"system", "df", "--format", "json"}))
        if (t.value("Type").toString() == "Images") d.images = dockerBytes(t.value("Size").toString());
    const int gb = docker::freeGb();
    d.vmFree = gb < 0 ? -1 : qint64(gb) << 30;
    d.at = QDateTime::currentSecsSinceEpoch();
    d.fresh = true;
    if (keep.isEmpty()) return d;
    // Kept whole or not at all: another pi-qemu cache may be reading it.
    QDir().mkpath(QFileInfo(keep).absolutePath());
    const QString tmp = keep + "." + QString::number(::getpid());
    files::write(tmp, QJsonDocument(d.toJson()).toJson());
    if (::rename(QFile::encodeName(tmp).constData(), QFile::encodeName(keep).constData()) != 0) QFile::remove(tmp);
    return d;
}

} // namespace cache
