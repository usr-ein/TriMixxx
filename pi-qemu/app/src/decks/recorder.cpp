#include "decks/recorder.h"

#include "decks/deck.h"
#include "util/fail.h"
#include "util/process.h"

#include <QFileInfo>
#include <QScopeGuard>
#include <QTextStream>
#include <QThread>

namespace recorder {

namespace {

const QString kDir = "/home/sam1902/Music/Mixxx/Recordings";

// A press of note 0x7A. toggle_recording acts on a value above 0 only, so
// each toggle is a press of its own, as a button's would be.
void toggle(Deck& d) { d.controls({"midi", "90", "7A", "7F", "80", "7A", "00"}); }

// The size of the newest recording; -1 if there is none.
qint64 newest(Deck& d) {
    proc::Options o;
    o.quiet = true;
    const proc::Result r = d.ssh().capture("f=$(ls -t " + kDir + "/*.wav 2>/dev/null | head -1); "
                                           "[ -n \"$f\" ] && stat -c %s \"$f\" || echo -1", o);
    return r.ok() ? r.text().toLongLong() : -1;
}

// Not proc::sleep: this also runs on the way out of a ^C.
bool growing(Deck& d) {
    const qint64 before = newest(d);
    QThread::msleep(1500);
    return newest(d) > before;
}

} // namespace

void record(Deck& d, int seconds, const QString& wav) {
    d.requireUp();
    // From a known state: anything an interrupted run left would pass for
    // this recording.
    if (d.ssh().run("mkdir -p " + kDir + " && rm -f " + kDir + "/*.wav") != 0)
        fail("cannot clear " + kDir + " on " + d.name());
    toggle(d);
    auto stop = qScopeGuard([&] {
        // Never leave the recorder running: off, if it still grows.
        try { if (growing(d)) toggle(d); } catch (const Failure&) {}
    });
    proc::sleep(1500);
    if (newest(d) < 0) {
        // Toggled off rather than on: a run before this one left it recording.
        toggle(d);
        proc::sleep(1500);
        if (newest(d) < 0) fail("nothing recorded: is [Recording] toggle_recording mapped to note 0x7A?");
    }
    proc::sleep(seconds * 1000);
    toggle(d);
    proc::sleep(1000);
    stop.dismiss();
    if (growing(d)) toggle(d);
    const proc::Result f = d.ssh().capture("ls -t " + kDir + "/*.wav | head -1");
    if (!f.ok() || f.text().isEmpty()) fail("no recording found in " + kDir);
    if (d.ssh().scpFrom(f.text(), QFileInfo(wav).absoluteFilePath()) != 0) fail("could not fetch " + f.text());
    QTextStream(stdout) << QFileInfo(wav).absoluteFilePath() << "\n";
}

} // namespace recorder
