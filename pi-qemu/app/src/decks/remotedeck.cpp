#include "decks/remotedeck.h"

#include "s3/controls.h"
#include "util/fail.h"
#include "util/paths.h"
#include "util/process.h"

#include <QFileInfo>

namespace {

// The port Mixxx opened for the deck's controller, from the sequencer graph.
// Not from Mixxx's pid: ALSA records the thread that made the port, not the
// process. ttymidi presents the S3 as a client called TriMixxx; whoever its
// port 0 feeds, bar the listeners known not to be Mixxx, is Mixxx. Its port
// takes events addressed to it (aseqsend -p) and refuses subscriptions, so
// `aconnect` cannot reach it. Resolved every time: client numbers change
// with every restart.
const char* kResolver = R"PY(
import re, subprocess, sys
graph = subprocess.run(["aconnect", "-l"], capture_output=True, text=True).stdout
names, targets, client, port = {}, [], None, None
skip = ("trimixxx-launchd", "pi-midi-daemon", "Midi Through")
for line in graph.splitlines():
    m = re.match(r"^client (\d+): (\S.*?) \[", line)
    if m:
        client, name = m.group(1), m.group(2).strip("'")
        names[client] = name
        continue
    m = re.match(r"^\s+(\d+) ", line)
    if m:
        port = m.group(1)
        continue
    if "Connecting To:" in line and names.get(client) == "TriMixxx" and port == "0":
        targets = re.findall(r"(\d+):(\d+)", line.split("Connecting To:")[1])
for c, p in targets:
    if names.get(c, "") not in skip:
        print(f"{c}:{p}")
        sys.exit(0)
sys.exit(1)
)PY";

QString hex(const QByteArray& b) { return QString::fromLatin1(b.toHex(' ').toUpper()); }

} // namespace

void RemoteDeck::requireUp() {
    if (m_up) return;
    if (!m_ssh.reachable(10)) fail(m_ssh.alias + " does not answer ssh");
    m_up = true;
}

QString RemoteDeck::wrappers() {
    if (!m_bin) {
        m_bin.emplace();
        if (!m_bin->isValid()) fail("cannot make a temporary directory");
        m_ssh.writeWrappers(m_bin->path());
    }
    return m_bin->path();
}

const Wiring& RemoteDeck::wiring() {
    if (!m_wiring) {
        Wiring w;
        QString err;
        if (!w.load(paths::units(), hostname(), &err)) fail(err);
        m_wiring = w;
    }
    return *m_wiring;
}

QString RemoteDeck::controls(const QStringList& words) {
    const std::optional<s3::Sequence> seq = s3::sequence(words, wiring());
    if (!seq) fail(words.value(0) + ": only an emulated deck has that (a real deck's controls are: " +
                   "press down up jog touch browse tempo midi)", 2);
    QString script = QString("set -eu\n"
                             "port=$(python3 - <<'PY'\n%1\nPY\n"
                             ") || { echo 'could not find Mixxx on the sequencer graph (is it running?)' >&2; exit 1; }\n")
                         .arg(QString::fromUtf8(kResolver).trimmed());
    for (const s3::Step& s : seq->steps) {
        script += "aseqsend -p \"$port\" '" + hex(s.bytes) + "'\n";
        if (s.holdMs > 0) script += QString("sleep %1\n").arg(s.holdMs / 1000.0, 0, 'f', 3);
    }
    if (m_ssh.run(script) != 0) fail("could not send to " + m_ssh.alias + "'s Mixxx");
    return seq->said;
}

void RemoteDeck::shot(const QString& png) {
    // The display's cookie: the session comes from the tty1 autologin, so an
    // ssh login has neither DISPLAY nor ~/.Xauthority; Xorg writes the cookie
    // to /tmp/serverauth.*. scrot -o overwrites: without it a second shot to
    // the same path silently fails.
    const QString grab = "set -eu\n"
                         "export DISPLAY=:0\n"
                         "auth=$(ls -t /tmp/serverauth.* 2>/dev/null | head -1)\n"
                         "[ -n \"${auth:-}\" ] && export XAUTHORITY=\"$auth\"\n"
                         "command -v scrot >/dev/null || { echo 'scrot is not installed on the deck: sudo apt install scrot' >&2; exit 1; }\n"
                         "scrot -o /tmp/deck-shot.png\n";
    if (m_ssh.run(grab) != 0) fail("could not grab the screen on " + m_ssh.alias);
    if (m_ssh.scpFrom("/tmp/deck-shot.png", QFileInfo(png).absoluteFilePath()) != 0)
        fail("could not fetch the screenshot from " + m_ssh.alias);
}
