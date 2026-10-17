#pragma once
// A deck's dev card, built on an emulated Pi: the stock Raspberry Pi OS card
// (image/stock.env), its first-boot seed (image/user-data.in, network-config),
// then every deploy step, run as against any deck -- what
// pi_config/fresh-install.md does to a new deck, as code.
//
// It builds in an instance of its own, build-DECK (pi-qemu deck list shows it,
// deck ssh reaches it), and the card ends up in .cache/build/DECK.img (the
// main checkout's: cache/cache.h). The slow first part -- the first boot,
// cloud-init and the base step (apt) -- is kept in .cache/base/ and reused
// while its inputs are unchanged:
// the stock card, the deck's name and panel overlay, the key, the password,
// the seed and the base step. trimixxx0's build ends by remaking the golden
// pair agents' instances start from.
//
// Silent and headless the whole time.
//
// What the card was built from goes beside it, .cache/build/DECK.json (a
// source::Record: the checkout's commit, and whether the checkout held
// exactly that commit's code for the deck, before the build and after it).
// Each build also keeps its attempt, .cache/build/DECK.build.json: building,
// built, failed, or unready (it stopped before the first boot: Docker, the
// key, QEMU), with its process and times, and how long it may run before it
// looks stuck (60 min, well past the longest real one: a base and all of
// Mixxx built again). One build of a deck runs at a time (Lock): two would
// share build-DECK, the first's machine pulled from under it.

#include "pipeline/source.h"
#include "util/filelock.h"

#include <QString>
#include <QStringList>

namespace imagebuild {

struct Options {
    bool rebuildBase = false; // make the base card again even if it is cached
};

QString card(const QString& deck);      // .cache/build/DECK.img
QStringList plan(const QString& deck);  // the steps, for the build window
// The card's path. The caller holds the deck's Lock, from before its log is emptied.
QString build(const QString& deck, const Options& o);

// A build of `deck`, held from before its log is emptied until its end;
// fails at once, saying which build runs, if another holds it.
class Lock {
public:
    explicit Lock(const QString& deck);

private:
    FileLock m_lock;
};
bool running(const QString& deck);     // a build of `deck` holds its lock now
QString lockFile(const QString& deck); // .cache/build/DECK.lock

// A build's attempt: the one running, or the last.
struct Attempt {
    QString state; // "building", "built", "failed", "unready", "interrupted"; empty: none yet
    qint64  pid = 0;
    QString started, ended; // ISO 8601, UTC
    QString why;            // a failure's message
    source::Record source;  // what the checkout held when it started
    int     stuckAfter = 0; // minutes it may run before it looks stuck; 0: the default

    qint64 minutes() const; // how long it has run, while building
    bool   stuck() const;   // building, for longer than it may
};
Attempt attempt(const QString& deck);     // .cache/build/DECK.build.json
QString attemptFile(const QString& deck);
QString logFile(const QString& deck);     // .cache/build/DECK.log
int stuckAfter(); // minutes: $PI_QEMU_STUCK_AFTER_MIN (a test's), else 60

// A build of `deck` that looks stuck stopped: its process (pi-qemu, nothing
// else: `program`, a test's stand-in), then its machine; its attempt says it
// failed, and why. Fails if none runs, or it doesn't look stuck.
void stop(const QString& deck, const QString& why, const QString& program = {});

} // namespace imagebuild
