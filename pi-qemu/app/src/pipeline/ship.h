#pragma once
// An update onto a running deck with a release card: the bundle streamed into
// RAUC's state partition (/tmp is RAM, and a bundle is over a gigabyte), the
// install into the other slot, a reboot into it as a trial, then what the deck
// says: its version, its slot, its health check, RAUC's status.

#include <QString>

class Deck;

namespace ship {

void ship(Deck& d, const QString& version);

} // namespace ship
