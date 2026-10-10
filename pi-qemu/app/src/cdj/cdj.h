#pragma once
// Emulated CDJs: a CDJ-2000NXS running Pioneer's own firmware on
// cdj2000-emulator (the submodule; docs/cdj-emulator.md), side by side with
// the emulated decks and on their links (board/link.h). Its state is kept
// outside every checkout, since it holds the firmware and what is made from
// it:
//
//   ~/.pi-qemu/cdj/ ($PI_QEMU_CDJ)
//     firmware/nxs/      the NXS updater, split and extracted (cdj firmware)
//     qemu-<commit>/     QEMU's pinned source, fetched once (cdj build)
//     NAME/              one CDJ:
//       cdj.json         how it was started: its link, MAC, media, checkout
//       pid, pi-qemu.log the `pi-qemu cdj run` behind it, and its output
//       link.sock        the emulator's NIC on the link (board/streamport.h)
//       run/             nxs_vm's run directory: its logs, screens and sockets
//
// A CDJ is booted, never restored: the emulator has no snapshot of one.
// `cdj run` is the emulator launcher's parent: it holds the CDJ's member on
// the link, and leaves it when the emulator exits.

#include "util/process.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace cdj {

// QEMU at the revision upstream tested: its master no longer takes the
// emulator's SH-4 patches (cdj2000-emulator issue #41).
inline constexpr char kQemuCommit[] = "55347990687e7bc5b6b0d624f290025726e8fbfa";
inline constexpr char kQemuUrl[] = "https://gitlab.com/qemu-project/qemu.git";

QString root();         // $PI_QEMU_CDJ, or ~/.pi-qemu/cdj
QString firmware();     // root()/firmware: the emulator's CDJ_FIRMWARE_DIR, holding nxs/
QString qemuSource();   // root()/qemu-<commit>: pristine, cloned into each checkout
QString emulator();     // this checkout's cdj2000-emulator/
QString qemu();         // emulator()/build/qemu/build/qemu-system-sh4
QString python();       // emulator()/.venv/bin/python, made by `cdj build`

// A CDJ's own MAC, from its name: locally administered, in pi-qemu's
// 02:43:44 ("CD") block, apart from the decks' 02:54:4d. The NXS takes its
// link-local address from the last two bytes, so the fifth is kept in
// 1..254, out of 169.254.0/24 and 169.254.255/24.
QString macFor(const QString& name);

// Fail, saying what to run, unless this checkout's emulator is checked out
// (and built; and the firmware extracted).
void requireCheckedOut();
void requireBuilt();
void requireFirmware();

// One of the emulator's Python tools (`python -m MODULE ARGS`), run from its
// checkout with its venv: its output kept, or passed through.
proc::Result captureTool(const QStringList& moduleAndArgs, bool quiet = false);
int          runTool(const QStringList& moduleAndArgs);

class Cdj {
public:
    explicit Cdj(const QString& name); // fails unless letters, digits, - and _
    static QStringList all();

    QString name() const { return m_name; }
    QString dir() const { return m_dir; }
    QString runDir() const { return m_dir + "/run"; }
    QString socket() const { return m_dir + "/link.sock"; }
    QString log() const { return m_dir + "/pi-qemu.log"; }

    bool   exists() const;
    qint64 pid() const; // its `cdj run`'s, while it runs; 0 otherwise
    bool   running() const { return pid() != 0; }
    QJsonObject config() const;
    QString link() const { return config().value("link").toString(); }
    QString mac() const { return config().value("mac").toString(); }

    struct Up {
        QString link;          // empty: no cable
        QString sd, usb;       // FAT32 images
        bool    testTrack = false, window = false;
    };
    void up(const Up& o);
    void rm();                  // stops it, then deletes its state
    int  serve();               // `cdj run`: the emulator, and its port on the link
    int  dev(const QStringList& args) const; // tools.cdj_main.dev on its run
    void requireRunning() const;

private:
    void waitUp(const QString& net);

    QString m_name, m_dir;
};

} // namespace cdj
