#pragma once
// The deploy steps, pi-qemu/deploy/NNN_name.sh and NNN_name.pi.sh, run on a
// deck in number order, with the exit-status protocol pi-qemu/deploy/lib.sh
// sets out: 0 done, 10 reboot before the next step, 11 the session restarts
// once the steps are over, 12 the step restarted it. The steps come from the
// code checkout, so a worktree deploys its own code.

#include <QString>
#include <QStringList>
#include <QVector>

class Deck;

namespace steps {

enum Status { Done = 0, Reboot = 10, RestartSession = 11, SessionRestarted = 12 };

struct Step {
    QString file;        // its path
    int     number = 0;
    QString name;        // "mixxx"
    bool    onDeck = false;      // NNN_name.pi.sh: into `sudo bash -s` on the deck
    bool    needsDocker = false; // a `# needs: docker` line in its header
    QString title() const { return QString("%1_%2").arg(number, 3, 10, QChar('0')).arg(name); }
};

// The steps in `dir`, in number order; fails on a stray name or a number used twice.
QVector<Step> all(const QString& dir);
// The named ones ("mixxx", "004", "004_mixxx"), in number order; none named: all.
QVector<Step> select(const QVector<Step>& all, const QStringList& names);

struct Options {
    bool rebootBetween = true; // a step's 10 reboots the deck before the next one
    bool waitReady = true;     // after a session restart, until Mixxx is ready
};
struct Outcome {
    bool rebootPending = false; // the boot flags changed, and no reboot has followed
    bool sessionRestarted = false;
};
Outcome run(Deck& deck, const QVector<Step>& steps, const Options& o = {});

// Reboot the deck and wait until it answers again, a new boot.
void reboot(Deck& deck);

} // namespace steps
