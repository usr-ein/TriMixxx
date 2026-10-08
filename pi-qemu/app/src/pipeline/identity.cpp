#include "pipeline/identity.h"

#include "util/envfile.h"
#include "util/fail.h"
#include "util/files.h"
#include "util/paths.h"
#include "util/process.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUuid>

namespace identity {

QString homeWifiUuid(const QString& ssid) {
    static const QUuid dns("{6ba7b810-9dad-11d1-80b4-00c04fd430c8}");
    return QUuid::createUuidV5(dns, ("home-wifi." + ssid).toUtf8()).toString(QUuid::WithoutBraces);
}

QString prepare(const QString& deck) {
    const QString d = paths::identities() + "/" + deck;
    const QString nm = d + "/NetworkManager";
    QDir().mkpath(d + "/ssh");
    QDir().mkpath(nm);
    if (!QFileInfo::exists(d + "/trimixxx.conf")) files::write(d + "/trimixxx.conf", (deck + "\n").toUtf8());

    // Host keys of its own, made once.
    if (QDir(d + "/ssh").entryList({"ssh_host_*_key"}, QDir::Files).isEmpty()) {
        QTemporaryDir t;
        QDir().mkpath(t.filePath("etc/ssh"));
        proc::Options quiet;
        quiet.quiet = true;
        if (!proc::capture("ssh-keygen", {"-A", "-f", t.path()}, quiet).ok()) fail("ssh-keygen -A failed");
        for (const QFileInfo& k : QDir(t.filePath("etc/ssh")).entryInfoList({"ssh_host_*"}, QDir::Files))
            if (!QFile::copy(k.absoluteFilePath(), d + "/ssh/" + k.fileName())) fail("cannot copy " + k.fileName());
    }

    // Its hotspot: named after it, on its own channel.
    const QString unit = paths::units() + "/" + deck + ".json";
    int channel = 6;
    if (QFileInfo::exists(unit)) channel = QJsonDocument::fromJson(files::read(unit)).object().value("hotspotChannel").toInt(6);
    const QString psk = envfile::read(paths::checkout() + "/pi_config/wifi-fallback/hotspot.env").value("HOTSPOT_PASSWORD");
    if (psk.isEmpty()) fail("no HOTSPOT_PASSWORD in pi_config/wifi-fallback/hotspot.env");
    QString hotspot = QString::fromUtf8(files::read(paths::piq() + "/release/hotspot.nmconnection.in"));
    hotspot.replace("@DECK@", deck).replace("@CHANNEL@", QString::number(channel)).replace("@PSK@", psk);
    files::write(nm + "/trimixxx-hotspot.nmconnection", hotspot.toUtf8(), true);

    // The home Wi-Fi, from secrets.env.
    const QString ssid = secrets::value("HOME_WIFI_SSID");
    if (!ssid.isEmpty()) {
        QString home = QString::fromUtf8(files::read(paths::piq() + "/release/home-wifi.nmconnection.in"));
        home.replace("@SSID@", ssid).replace("@PSK@", secrets::value("HOME_WIFI_PSK")).replace("@UUID@", homeWifiUuid(ssid));
        files::write(nm + "/home-wifi.nmconnection", home.toUtf8(), true);
    }
    // The emulated deck's ssh rides pi-qemu's management NIC.
    if (deck == "trimixxx0") {
        QFile::remove(nm + "/pi-qemu-home.nmconnection");
        QFile::copy(paths::piq() + "/release/pi-qemu-home.nmconnection", nm + "/pi-qemu-home.nmconnection");
    }

    QTextStream out(stdout);
    for (const QFileInfo& k : QDir(d + "/ssh").entryInfoList({"ssh_host_*_key.pub"}, QDir::Files))
        out << deck << "'s host key: " << proc::capture("ssh-keygen", {"-lf", k.absoluteFilePath()}).text() << "\n";
    out.flush();
    return d;
}

} // namespace identity
