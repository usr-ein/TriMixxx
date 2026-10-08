#pragma once
// Docker, which builds the deck's arm64 binaries and runs the release
// container. Its VM disk fills up with Mixxx builds, and a full one fails a
// build minutes in: pi-qemu checks first, and never frees space itself --
// that is the person's call (docker system df).

namespace docker {

void requireRunning();
int  freeGb();                  // free space in Docker's VM disk; -1 if unknown
void requireFree(int gb);       // fails, saying how much there is

} // namespace docker
