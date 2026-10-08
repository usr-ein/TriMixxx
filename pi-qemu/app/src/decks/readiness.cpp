#include "decks/readiness.h"

#include "util/fail.h"
#include "util/process.h"
#include "util/tool.h"

#include <QElapsedTimer>

namespace readiness {

void waitSsh(const SshTarget& t, int seconds, const std::function<void()>& alive) {
    QElapsedTimer clock;
    clock.start();
    while (!t.reachable(2)) {
        if (alive) alive();
        if (clock.elapsed() > seconds * 1000LL) fail(QString("%1: no ssh after %2 s").arg(t.alias).arg(seconds));
        proc::sleep(200);
    }
}

Mixxx mixxx(const SshTarget& t) {
    static const QString check = QStringLiteral(
        "p=$(pgrep -xo mixxx) || exit 1\n"
        "[ \"$(readlink /proc/$p/fd/* 2>/dev/null | grep -cx /tmp/mixxx/mixxx.log)\" -gt 0 ] || exit 1\n"
        "grep -q 'Started stream successfully' /tmp/mixxx/mixxx.log || exit 1\n"
        "grep -q 'Opening controller: \"TriMixxx\"' /tmp/mixxx/mixxx.log || exit 1\n"
        "b=$(pgrep -xo ttymidi) || exit 1\n"
        "[ \"$(ps -o etimes= -p $b)\" -ge \"$(ps -o etimes= -p $p)\" ] || exit 2\n");
    proc::Options o;
    o.quiet = true;
    o.timeoutMs = 20000;
    const int status = proc::capture("ssh", t.args({"-o", "ConnectTimeout=2"}, check), o).status;
    return status == 0 ? Mixxx::Ready : status == 2 ? Mixxx::Deaf : Mixxx::NotYet;
}

void waitMixxx(const SshTarget& t, int seconds, const QString& target, const std::function<void()>& alive) {
    QElapsedTimer clock;
    clock.start();
    for (;;) {
        const Mixxx m = mixxx(t);
        if (m == Mixxx::Ready) return;
        if (m == Mixxx::Deaf)
            fail("Mixxx started before the MIDI bridge restarted, so the S3 no longer reaches it: " +
                 tool("deck ssh " + target + " 'sudo systemctl restart getty@tty1'") + ", then " +
                 tool("deck ready " + target));
        if (alive) alive();
        if (clock.elapsed() > seconds * 1000LL)
            fail(QString("Mixxx not ready after %1 s: ").arg(seconds) +
                 tool("deck ssh " + target + " 'tail -30 /tmp/mixxx/stderr.log'"));
        proc::sleep(500);
    }
}

} // namespace readiness
