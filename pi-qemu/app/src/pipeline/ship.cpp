#include "pipeline/ship.h"

#include "decks/deck.h"
#include "pipeline/release.h"
#include "util/buildlog.h"
#include "util/diskuse.h"
#include "util/fail.h"
#include "util/meter.h"
#include "util/process.h"
#include "util/tool.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

namespace ship {

void ship(Deck& d, const QString& v) {
    const QString bundle = release::outDir(v) + "/trimixxx-" + v + ".raucb";
    if (!QFileInfo::exists(bundle)) fail("no " + bundle + ": " + tool("release build"));
    d.requireUp();
    // Only a release card's seal installs RAUC's configuration, and a dev
    // card has /var/lib/rauc all the same (the History's): refused here,
    // before a gigabyte crosses the network onto a card that cannot take it.
    if (d.ssh().run("test -e /etc/rauc/system.conf") != 0)
        fail(d.name() + " runs a dev card (no /etc/rauc/system.conf): an update is for a release card");
    const QString staged = "/var/lib/rauc/trimixxx-" + v + ".raucb";
    const qint64 size = QFileInfo(bundle).size();

    // The bundle fed into ssh by us, which counts what ssh has taken: the
    // bar's figures, whatever the link.
    buildlog::say("sending " + v + "'s bundle (" + diskuse::text(size) + ") to " + d.name());
    {
        Meter m(size, Meter::Bytes);
        qint64 sent = 0;
        proc::Options stream;
        stream.input = proc::Input::File;
        stream.file = bundle;
        const int status = d.ssh().follow(
            "sudo sh -c " + proc::quote("cat > " + staged), stream,
            [&m](const QString& line, bool err) { m.say(line, err); },
            [&](qint64 taken) {
                sent = taken;
                m.update(taken);
            });
        if (status != 0 || sent != size)
            fail("could not copy the bundle onto " + d.name() + ": it stopped at " + m.sofar());
    }

    // RAUC's own percent: off a terminal (ssh runs it without one), `rauc
    // install` prints "NN% what it does" at each change, every 1% of an image
    // it copies included. Its other lines pass through as they are.
    buildlog::say("installing " + v + " on " + d.name() + ": RAUC writes it into the other slot");
    {
        static const QRegularExpression progress(R"(^\s*(\d{1,3})% (.*)$)");
        Meter m(100, Meter::Percent);
        const int status = d.ssh().follow(
            QString("sudo rauc install %1; r=$?; sudo rm -f %1; exit $r").arg(staged), {},
            [&m](const QString& line, bool err) {
                const QRegularExpressionMatch p = progress.match(line);
                if (!err && p.hasMatch()) m.update(p.captured(1).toInt(), p.captured(2));
                else m.say(line, err);
            },
            [&m](qint64) { m.tick(); });
        if (status != 0) fail("RAUC did not install " + v + " on " + d.name());
    }
    QTextStream(stdout) << d.name() << ": installed " << v << "; rebooting into it as a trial" << Qt::endl;
    decks::reboot(d, "into " + v + ", a trial", 240);
    d.ssh().run("cat /etc/trimixxx-release; /usr/lib/rauc/rpi-tryboot booted; "
                "sudo systemctl is-active trimixxx-health || true; rauc status");
}

} // namespace ship
