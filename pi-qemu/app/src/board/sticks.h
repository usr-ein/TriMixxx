#pragma once
// USB sticks for the emulated deck: any USB storage device on this Mac, an
// image file, or a folder (or a .stick file describing one) served as a FAT32
// stick made up on the fly (FatVolume), inserted into and unplugged from the
// Pi's xHCI on demand, two at most -- the deck's two slots (dj-usb's
// DJ_USB_1/2). Read-only, as the deck mounts them, so nothing is ever written
// to a stick, nor to a folder.
//
// Each stick reads through a QEMU throttle group of its own, so it can be as
// slow as a real one -- a cheap USB 2 stick gives 3 to 5 MB/s -- and its speed
// changes while it is in (speed()), which is how to try a deck against slow
// sticks without a drawer full of them.

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>
#include <functional>
#include <map>
#include <memory>

#include "board/nbdserver.h"

class Machine;

// How fast a stick reads: bytes a second, and reads a second whatever their
// size (what makes a cheap stick's small reads slow too). 0: no limit.
struct StickSpeed {
    qint64 bytesPerSecond = 0;
    int    readsPerSecond = 0;
    static bool parse(const QString& text, StickSpeed* out); // "4M", "3.7M/300", "full"
    QString text() const;                                    // "4.0 MB/s, 300 reads/s"
    QJsonObject limits() const;                              // as QEMU's ThrottleLimits
};

struct HostStick {
    QString id;      // "disk4", or an image's, a .stick file's or a folder's path
    QString name;    // what the stick calls itself / its model
    qint64  size = 0;
    bool    inserted = false;
};

class Sticks : public QObject {
    Q_OBJECT
public:
    static constexpr int kSlots = 2;
    // imagesDir: stick images (*.img) and folder sticks (*.stick) offered
    // alongside the Mac's own USB storage.
    Sticks(Machine* m, QString imagesDir, QObject* parent = nullptr);

    QVector<HostStick> list();   // every USB storage device on the host, plus inserted images
    using Done = std::function<void(bool ok, const QString& message)>;
    void insert(const QString& id, Done done);
    void unplug(const QString& id, Done done);
    // How fast it reads, now if it is in, or from the moment it goes in.
    void setSpeed(const QString& id, const StickSpeed& speed, Done done);
    QString speedOf(const QString& id) const;
    // What the Pi has read off a folder stick since it went in (or since the
    // last reset), by file; empty for any other kind of stick.
    QString reads(const QString& id, bool reset);
    void forgetAll();            // the board stopped: nothing is inserted any more

signals:
    void changed();

private:
    QString resolve(const QString& name) const;
    void    plug(const QString& id, int n, const QJsonObject& file, const StickSpeed& speed, const QString& note, Done done);
    void    insertFolder(const QString& id, int n, Done done);
    bool    passFd(int fd, int fdset, QString* error);
    void    dropFdset(int fdset);
    int     openRawDisk(const QString& disk, QString* error);

    Machine*             m_machine;
    QHash<QString, int>  m_inserted; // id -> device number (stickN)
    QSet<QString>        m_pending;  // folders being walked, not yet in
    std::map<int, std::unique_ptr<NbdServer>> m_servers; // device number -> a folder stick's server
    QHash<QString, StickSpeed> m_speeds;      // id -> a speed asked for (speed()), kept for its next insert
    QHash<int, StickSpeed>     m_deviceSpeed; // device number -> how fast it reads now
    QString              m_imagesDir;
    int                  m_fdMon = -1; // the one connection to QEMU's fd monitor
    int                  m_next = 0;
};
