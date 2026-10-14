#include "pipeline/ship.h"

#include "decks/deck.h"
#include "pipeline/release.h"
#include "util/fail.h"
#include "util/process.h"
#include "util/tool.h"

#include <QFileInfo>
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
    proc::Options stream;
    stream.input = proc::Input::File;
    stream.file = bundle;
    if (d.ssh().run("sudo sh -c " + proc::quote("cat > " + staged), stream) != 0)
        fail("could not copy the bundle onto " + d.name());
    if (d.ssh().run(QString("sudo rauc install %1; r=$?; sudo rm -f %1; exit $r").arg(staged)) != 0)
        fail("RAUC did not install " + v + " on " + d.name());
    QTextStream(stdout) << d.name() << ": installed " << v << "; rebooting into it as a trial" << Qt::endl;
    decks::reboot(d, "into " + v + ", a trial", 240);
    d.ssh().run("cat /etc/trimixxx-release; /usr/lib/rauc/rpi-tryboot booted; "
                "sudo systemctl is-active trimixxx-health || true; rauc status");
}

} // namespace ship
