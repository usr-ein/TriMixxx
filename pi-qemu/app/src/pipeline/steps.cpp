#include "pipeline/steps.h"

#include "decks/deck.h"
#include "decks/readiness.h"
#include "util/buildlog.h"
#include "util/docker.h"
#include "util/fail.h"
#include "util/files.h"
#include "util/paths.h"
#include "util/process.h"
#include "util/tool.h"
#include "worktree/worktree.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>

namespace steps {

QVector<Step> all(const QString& dir) {
    static const QRegularExpression stepName(R"(^(\d{3})_([a-z0-9][a-z0-9-]*)(\.pi)?\.sh$)");
    QVector<Step> out;
    for (const QFileInfo& f : QDir(dir).entryInfoList({"*.sh"}, QDir::Files, QDir::Name)) {
        if (f.fileName() == "lib.sh") continue;
        const auto m = stepName.match(f.fileName());
        if (!m.hasMatch()) fail(f.filePath() + ": a deploy step is NNN_name.sh or NNN_name.pi.sh");
        Step s;
        s.file = f.absoluteFilePath();
        s.number = m.captured(1).toInt();
        s.name = m.captured(2);
        s.onDeck = !m.captured(3).isEmpty();
        // Its header: the comment lines up to the first other line.
        for (const QByteArray& line : files::read(s.file).split('\n')) {
            if (!line.startsWith('#') && !line.trimmed().isEmpty()) break;
            if (line.trimmed() == "# needs: docker") s.needsDocker = true;
        }
        for (const Step& o : out)
            if (o.number == s.number || o.name == s.name)
                fail("two deploy steps share a number or a name: " + o.title() + ", " + s.title());
        out << s;
    }
    if (out.isEmpty()) fail("no deploy steps in " + dir);
    return out;
}

QVector<Step> select(const QVector<Step>& all, const QStringList& names) {
    if (names.isEmpty() || names == QStringList{"all"}) return all;
    QVector<Step> out;
    for (const Step& s : all) {
        const bool named = std::any_of(names.begin(), names.end(), [&](const QString& n) {
            return n == s.name || n == s.title() || n == QString("%1").arg(s.number, 3, 10, QChar('0'));
        });
        if (named) out << s;
    }
    for (const QString& n : names) {
        const bool known = std::any_of(all.begin(), all.end(), [&](const Step& s) {
            return n == s.name || n == s.title() || n == QString("%1").arg(s.number, 3, 10, QChar('0'));
        });
        if (!known) {
            QStringList titles;
            for (const Step& s : all) titles << s.title();
            fail("no deploy step " + n + " (the steps: " + titles.join(' ') + ")", 2);
        }
    }
    return out;
}

void reboot(Deck& deck) {
    proc::Options quiet;
    quiet.quiet = true;
    quiet.timeoutMs = 20000;
    const QString old = deck.ssh().capture("cat /proc/sys/kernel/random/boot_id", quiet).text();
    if (old.isEmpty()) fail("cannot reach " + deck.name() + " to reboot it");
    buildlog::say("rebooting " + deck.name() + " into its new boot flags");
    deck.ssh().capture("sudo systemd-run --quiet --on-active=1 systemctl reboot", quiet);
    QElapsedTimer t;
    t.start();
    for (;;) {
        proc::sleep(2000);
        if (deck.emulated()) deck.requireUp(); // its pi-qemu goes on through the reboot
        proc::Options o = quiet;
        o.timeoutMs = 10000;
        const proc::Result r = proc::capture("ssh", deck.ssh().args({"-o", "ConnectTimeout=2", "-o", "BatchMode=yes"},
                                                                    "cat /proc/sys/kernel/random/boot_id"), o);
        if (r.ok() && !r.text().isEmpty() && r.text() != old) break;
        if (t.elapsed() > 300000) fail(deck.name() + " did not come back within 300 s of its reboot");
    }
    QTextStream(stdout) << deck.name() << ": back after " << t.elapsed() / 1000 << " s" << Qt::endl;
}

Outcome run(Deck& deck, const QVector<Step>& steps, const Options& o) {
    deck.requireUp();
    const QString host = deck.hostname();
    const QString unit = paths::units() + "/" + host + ".json";
    QString panel;
    if (QFileInfo::exists(unit))
        panel = QJsonDocument::fromJson(files::read(unit)).object().value("panelOverlay").toString();
    if (std::any_of(steps.begin(), steps.end(), [](const Step& s) { return s.needsDocker; })) docker::requireFree(6);
    // A worktree starts without the submodules these steps build from.
    worktree::prepare(true);

    QStringList titles;
    for (const Step& s : steps) titles << s.title();
    buildlog::say("deploying onto " + deck.name() + " (" + host + "): " + titles.join(' '));
    proc::Options env;
    env.cwd = paths::checkout();
    env.env = {{"DECK", host}, {"REPO", paths::checkout()}, {"UNIT_JSON", unit}, {"PANEL_OVERLAY", panel}, {"PIQEMU_STEP", "1"}};
    env.pathFirst = {deck.wrappers()};

    Outcome out;
    bool stale = false;
    for (const Step& s : steps) {
        if (out.rebootPending && o.rebootBetween) {
            reboot(deck);
            out.rebootPending = false;
            out.sessionRestarted = true; // a new boot, a new session
            stale = false;
        }
        buildlog::say("deploy " + s.title() + " on " + deck.name());
        int status;
        if (s.onDeck) {
            proc::Options pipe;
            pipe.input = proc::Input::File;
            pipe.file = s.file;
            const QString command = "sudo env " + proc::join({"DECK=" + host, "PANEL_OVERLAY=" + panel, "PIQEMU_STEP=1"}) +
                                    " bash -s";
            status = proc::run("ssh", deck.ssh().args({}, command), pipe);
        } else {
            status = proc::run("bash", {s.file}, env);
        }
        switch (status) {
        case Done: break;
        case Reboot: out.rebootPending = true; break;
        case RestartSession: stale = true; break;
        case SessionRestarted: stale = false; out.sessionRestarted = true; break;
        default: fail(QString("deploy step %1 failed (exit %2)").arg(s.title()).arg(status));
        }
    }
    if (stale) {
        // Mixxx opens the MIDI bridge's and the launcher's ports once, at its
        // start: a step that restarted either left it deaf to them.
        buildlog::say("restarting the session on " + deck.name() + ": Mixxx opens the MIDI ports only at its start");
        if (deck.ssh().run("sudo systemctl restart getty@tty1.service") != 0) fail("could not restart the session");
        out.sessionRestarted = true;
    }
    if (out.sessionRestarted && o.waitReady) {
        readiness::waitMixxx(deck.ssh(), 180, deck.spelled(), [&deck] { deck.requireUp(); });
        QTextStream(stdout) << deck.name() << ": Mixxx is ready (sound open, the S3 connected)" << Qt::endl;
    }
    if (out.rebootPending && o.rebootBetween)
        QTextStream(stdout) << "The boot flags changed: they apply from " << deck.name() << "'s next reboot ("
                            << tool("deck ssh " + deck.spelled() + " sudo reboot") << ")" << Qt::endl;
    return out;
}

} // namespace steps
