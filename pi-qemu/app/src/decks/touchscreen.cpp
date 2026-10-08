#include "decks/touchscreen.h"

#include "decks/deck.h"
#include "util/fail.h"
#include "util/process.h"

namespace touchscreen {

namespace {

// The display and its cookie, as decks/remotedeck's shot finds them.
proc::Result x(Deck& d, const QString& commands) {
    const QString script = "set -eu\n"
                           "export DISPLAY=:0\n"
                           "auth=$(ls -t /tmp/serverauth.* 2>/dev/null | head -1)\n"
                           "[ -n \"${auth:-}\" ] && export XAUTHORITY=\"$auth\"\n"
                           "command -v xdotool >/dev/null || { echo 'xdotool is not installed: sudo apt install xdotool' >&2; exit 1; }\n" +
                           commands + "\n";
    const proc::Result r = d.ssh().capture(script);
    if (!r.ok()) fail("xdotool on " + d.name() + " failed");
    return r;
}

QString seconds(int ms) { return QString::number(ms / 1000.0, 'f', 3); }

} // namespace

void tap(Deck& d, int px, int py) {
    // A tap is a short press: `xdotool click 1` puts press and release ~12 ms
    // apart and Qt's buttons ignore it. 120 ms is well under the 600 ms of a
    // long press.
    x(d, QString("xdotool mousemove %1 %2 mousedown 1; sleep 0.12; xdotool mouseup 1").arg(px).arg(py));
}

void longpress(Deck& d, int px, int py, int ms) {
    x(d, QString("xdotool mousemove %1 %2 mousedown 1; sleep %3; xdotool mouseup 1").arg(px).arg(py).arg(seconds(ms)));
}

void drag(Deck& d, int x1, int y1, int x2, int y2, int ms) {
    const int steps = 12;
    QString cmd = QString("xdotool mousemove %1 %2 mousedown 1").arg(x1).arg(y1);
    for (int i = 1; i <= steps; i++)
        cmd += QString("; xdotool mousemove %1 %2; sleep %3")
                   .arg(x1 + (x2 - x1) * i / steps).arg(y1 + (y2 - y1) * i / steps).arg(seconds(ms / steps));
    x(d, cmd + "; xdotool mouseup 1");
}

void keys(Deck& d, const QStringList& keysyms) {
    if (keysyms.isEmpty()) fail("key KEYSYM...: e.g. key Down Return", 2);
    x(d, "xdotool key --delay 120 " + proc::join(keysyms));
}

QString where(Deck& d) { return x(d, "xdotool getmouselocation").text(); }

} // namespace touchscreen
