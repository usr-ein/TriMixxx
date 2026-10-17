#pragma once
// The golden pair every emulated deck starts from (.cache/golden/trimixxx0.img
// and .state, made by Instance::golden from a built card), kept main's
// latest. Sam, 2026-10-16: "I want emulations to run the latest firmware."
//
// Its record, .cache/golden/trimixxx0.json (a source::Record), says what code
// it holds. When origin/main has moved on in what goes onto a deck
// (source::deckPaths), the golden is made again before a deck starts from
// it: `image build trimixxx0` in the main checkout, a process of its own,
// whose card then becomes the golden. `deck up` waits for that, or for the
// one running already; crew merge leaves a keeper to it (`image golden
// --background`), which goes on until the golden is main's latest, main
// having moved on meanwhile or not.
//
// The rules (Sam's and the nightman's calls, 2026-10-16/17):
//  - a whole image build each time, from stock (its base cached), so nothing
//    the repo stopped putting on a deck lingers;
//  - from the main checkout itself, and only while it holds main's code for
//    the deck (source::differs: by content, whatever its branch); otherwise
//    the golden stays as it is, and every check says why;
//  - a main whose build failed is not tried again by itself (retry does);
//    one whose build could not start (Docker, the key, QEMU) is;
//  - a remake running past the time it may take (imagebuild::stuckAfter)
//    looks stuck: nobody waits for it, and decks start from the last golden
//    until the nightman or a person stops it (`image golden --clear`, once
//    its log has been quiet 10 min: imagebuild::stop);
//  - only main's code, so anyone may start one: `deck golden CARD` and
//    `image build trimixxx0` by hand stay the person's call, and a golden
//    they make from other code is made again at the next check.

#include "pipeline/source.h"
#include "util/filelock.h"

#include <QString>

#include <functional>

namespace golden {

enum class State {
    Current,  // main's latest
    Stale,    // to be made again, and nothing was started (check only)
    Remaking, // being made again: started now, or running already
    Failed,   // main's build failed, and is not tried again by itself
    Stuck,    // its remake has run past the time it may: not waited for
    Waiting,  // it can't be made now: the main checkout, or the build couldn't start
    Unknown,  // no main to compare it with
};

struct Options {
    bool start = true;  // start a remake when one is needed; false: only say
    bool wait = true;   // until a remake, started or found running, is over
    bool retry = false; // a main whose build failed: again
    bool quiet = false; // say nothing when it is main's latest already
    // How a remake starts: `image build trimixxx0` in the main checkout,
    // detached; returns its process. Tests give their own.
    std::function<qint64()> spawn;
};

struct Outcome {
    State   state = State::Unknown;
    QString main;           // origin/main's commit, as last read
    source::Record golden;  // what the golden holds now
};

// The golden made main's latest if it can be, as `o` says, with a line for
// each thing it does or can't do. origin/main is fetched first, and read
// again after each wait: a main that moved on meanwhile is the one it is
// made for.
Outcome ensure(const Options& o = {});

// `image golden --background`: when the golden is to be made again, or is
// being made, a keeper of its own does it (`image golden --keeper`,
// detached), unless one runs already; returns what the check found.
Outcome keepInBackground(bool retry);
// The keeper itself: ensure(), holding the keeper's lock; none if another holds it.
Outcome keep(bool retry);

// `image golden --clear`: a remake that looks stuck, stopped (imagebuild::stop).
void clear();

QString recordFile();    // .cache/golden/trimixxx0.json
source::Record record(); // the golden's; empty if it has none

// The golden pair, held while a deck clones it (Read) or while it is made
// (Make: Instance::golden, one at a time) and swapped (Swap).
class Hold {
public:
    enum Mode { Read, Make, Swap };
    explicit Hold(Mode mode);

private:
    FileLock m_lock;
};

} // namespace golden
