#include "util/docker.h"

#include "util/fail.h"
#include "util/process.h"

#include <QRegularExpression>

namespace docker {

void requireRunning() {
    proc::Options o;
    o.quiet = true;
    if (!proc::capture("docker", {"info"}, o).ok()) fail("Docker is not running: start Docker Desktop");
}

int freeGb() {
    proc::Options o;
    o.quiet = true;
    const proc::Result r = proc::capture("docker", {"run", "--rm", "--pull", "missing", "debian:trixie",
                                                    "df", "-BG", "--output=avail", "/"}, o);
    if (!r.ok()) return -1;
    QString digits = r.text().split('\n').last();
    digits.remove(QRegularExpression("[^0-9]"));
    return digits.isEmpty() ? -1 : digits.toInt();
}

void requireFree(int gb) {
    requireRunning();
    const int free = freeGb();
    if (free < 0) fail("cannot tell how full Docker's disk is (docker run debian:trixie df failed)");
    if (free < gb)
        fail(QString("Docker's disk has %1 GB free, a build needs ~%2: free some first (docker system df)")
                 .arg(free).arg(gb));
}

bool vmUp() {
    // Its hypervisor, com.docker.sailor (Docker VMM), is a process only while
    // the VM runs; by its path, as the command's first word, since the short
    // name is cut to 16 characters. pgrep failing to run: up, so the caller
    // reads Docker as it always did.
    proc::Options o;
    o.quiet = true;
    return proc::capture("pgrep", {"-qf", "^[^ ]*/com\\.docker\\.sailor( |$)"}, o).status != 1;
}

} // namespace docker
