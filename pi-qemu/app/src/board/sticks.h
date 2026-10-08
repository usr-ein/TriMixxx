#pragma once
// USB sticks for the emulated deck: any USB storage device on this Mac (or an
// image file), inserted into and unplugged from the Pi's xHCI on demand, two at
// most -- the deck's two slots (dj-usb's DJ_USB_1/2). Read-only, as the deck
// mounts them, so nothing is ever written to a stick.

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

class Machine;

struct HostStick {
    QString id;      // "disk4", or an image file's path
    QString name;    // what the stick calls itself / its model
    qint64  size = 0;
    bool    inserted = false;
};

class Sticks : public QObject {
    Q_OBJECT
public:
    static constexpr int kSlots = 2;
    // imagesDir: stick images (*.img) offered alongside the Mac's own USB storage.
    Sticks(Machine* m, QString imagesDir, QObject* parent = nullptr);

    QVector<HostStick> list();   // every USB storage device on the host, plus inserted images
    using Done = std::function<void(bool ok, const QString& message)>;
    void insert(const QString& id, Done done);
    void unplug(const QString& id, Done done);
    void forgetAll();            // the board stopped: nothing is inserted any more

signals:
    void changed();

private:
    bool    passFd(int fd, int fdset, QString* error);
    void    dropFdset(int fdset);
    int     openRawDisk(const QString& disk, QString* error);

    Machine*             m_machine;
    QHash<QString, int>  m_inserted; // id -> device number (stickN)
    QString              m_imagesDir;
    int                  m_fdMon = -1; // the one connection to QEMU's fd monitor
    int                  m_next = 0;
};
