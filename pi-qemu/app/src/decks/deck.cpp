#include "decks/deck.h"

#include "cli/args.h"
#include "decks/emulateddeck.h"
#include "decks/remotedeck.h"
#include "util/buildlog.h"
#include "util/fail.h"
#include "util/process.h"

#include <QElapsedTimer>
#include <QTextStream>

QString Deck::hostname() {
    if (m_hostname.isEmpty()) {
        proc::Options o;
        o.timeoutMs = 30000;
        const proc::Result r = ssh().capture("hostname", o);
        if (!r.ok() || r.text().isEmpty()) fail("cannot reach " + name() + " over ssh");
        m_hostname = r.text();
    }
    return m_hostname;
}

namespace decks {

std::unique_ptr<Deck> target(cli::Args& a) {
    if (a.has("host")) return std::make_unique<RemoteDeck>(a.value("host"));
    return std::make_unique<EmulatedDeck>(a.take("TARGET: an emulated deck's NAME, or --host ALIAS"));
}

QString hostOptionHelp() {
    return "a real deck, by its ssh alias (trimixxx-pi), instead of an emulated deck's NAME";
}

QString bootId(Deck& d, int connectTimeout) {
    proc::Options o;
    o.quiet = true;
    o.timeoutMs = (connectTimeout + 10) * 1000;
    const proc::Result r = proc::capture(
        "ssh", d.ssh().args({"-o", QString("ConnectTimeout=%1").arg(connectTimeout), "-o", "BatchMode=yes"},
                            "cat /proc/sys/kernel/random/boot_id"), o);
    return r.ok() ? r.text() : QString();
}

QString reboot(Deck& d, const QString& why, int seconds) {
    const QString old = bootId(d);
    if (old.isEmpty()) fail("cannot reach " + d.name() + " to reboot it");
    buildlog::say("rebooting " + d.name() + " " + why);
    proc::Options o;
    o.quiet = true;
    o.timeoutMs = 20000;
    d.ssh().capture("sudo systemd-run --quiet --on-active=1 systemctl reboot", o);
    QElapsedTimer t;
    t.start();
    for (;;) {
        proc::sleep(2000);
        if (d.emulated()) d.requireUp(); // its pi-qemu goes on through the reboot
        const QString now = bootId(d, 2);
        if (!now.isEmpty() && now != old) {
            QTextStream(stdout) << d.name() << ": back after " << t.elapsed() / 1000 << " s" << Qt::endl;
            return now;
        }
        if (t.elapsed() > seconds * 1000LL) fail(QString("%1 did not come back within %2 s of its reboot").arg(d.name()).arg(seconds));
    }
}

} // namespace decks
