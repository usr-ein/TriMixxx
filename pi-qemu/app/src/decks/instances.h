#pragma once
// Emulated decks side by side, one per agent or person, each its own copy of
// the card, ssh port, ssh alias and control socket, in
// ~/.pi-qemu/instances/NAME/ (short paths: the run directory holds Unix
// sockets, which macOS caps at 104 bytes; on the repo's disk, so the cards
// stay APFS clones):
//
//   card.img    its card
//   state       its saved machine, while it is suspended (deck stop)
//   card.run/   pi-qemu run's directory: control.sock, ssh.port, qemu.pid,
//               qemu.log, console.sock and console.log
//   ssh_config  Host NAME and deck: 127.0.0.1, its port, the key the cards trust
//   bin/        ssh and scp through ssh_config (the alias `deck` for deploy steps)
//   link        the link its eth0 is on (board/link.h), if any: kept for every up
//   pid, command, pi-qemu.log    the pi-qemu run behind it
//
// Starting is a restore, not a boot: a new instance gets clones of the golden
// card and of the machine saved with it (pi-qemu/.cache/golden/trimixxx0.img
// and .state, made by `deck golden`), and resumes that machine -- booted,
// Mixxx running -- in seconds. `stop` saves an instance the same way. A
// restore needs the card it was saved with, so a saved machine is used once:
// it is gone the moment it starts, and the card goes on from there.
//
// On a link, a deck is a device of its own: its eth0 MAC comes from its name
// (links::macFor), and so do its link-local address and its player number.
// A restored deck wakes up as the golden one, so it starts unplugged, takes
// its own MAC, and is plugged in once Mixxx has let go of the golden deck's
// address: nothing on the link ever hears the clone.

#include "util/sshtarget.h"

#include <QString>
#include <QStringList>

class Instance {
public:
    explicit Instance(const QString& name); // fails unless letters, digits, - and _
    static QStringList all();

    QString name() const { return m_name; }
    QString dir() const { return m_dir; }
    QString card() const { return m_dir + "/card.img"; }
    QString state() const { return m_dir + "/state"; }
    QString runDir() const { return m_dir + "/card.run"; }
    QString binDir() const { return m_dir + "/bin"; }
    QString controlSocket() const { return runDir() + "/control.sock"; }
    QString consoleSocket() const { return runDir() + "/console.sock"; }
    QString log() const { return m_dir + "/pi-qemu.log"; } // the run behind it
    SshTarget ssh() const { return {m_name, m_dir + "/ssh_config"}; }

    bool   exists() const;
    QString link() const;   // the link its eth0 is on; empty for none
    qint64 pid() const;     // pi-qemu's while it runs; 0 otherwise
    bool   running() const { return pid() != 0; }
    int    sshPort() const; // 0 if unknown
    void   requireRunning() const;

    struct Up {
        bool        window = false, fresh = false, boot = false;
        QString     from;    // a card of its own, booted
        QString     link;    // its eth0's link: NET, "none" for none; empty: as it was
        QStringList runArgs; // for pi-qemu run, as they are
        QString     deck = "trimixxx0"; // whose wiring its controls follow
        int         sshWait = 300;      // seconds: a stock card's first boot takes longer
    };
    void up(const Up& o);
    void stop();   // suspend: the machine saved, then off
    void down();   // a clean poweroff
    void kill();   // pull the plug
    void remove(); // pull the plug, and delete it

    // The golden pair from `card`: booted headless until Mixxx plays, saved.
    static void golden(QString card);

private:
    Instance(const QString& name, const QString& dir) : m_name(name), m_dir(dir) {}
    void launch(const QStringList& runArgs, const QString& deck = "trimixxx0");
    void checkAlive() const;  // fails, with its logs, if pi-qemu has exited
    void pullPlug();          // pi-qemu and its QEMU stopped, whatever it takes
    void suspend();
    void writeSshConfig(int port);
    void identify(const QString& mac); // a restored deck's eth0 becomes its own
    void plug();                       // its cable into its link
    QString heardPlayer(int seconds);  // what its keep-alives on the link say, once they do
    void announce(int seconds);        // until they name a player it can be browsed as

    QString m_name, m_dir;
};
