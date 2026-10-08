#pragma once
// Mixxx's own recording of the main mix -- the sum that leaves the DAC, aux
// return, effects and master gain included -- for `seconds`, fetched to the
// Mac. Mixxx's recorder is toggled by MIDI note 0x7A on the deck's
// controller port (TriMixxx.midi.xml, for this; the S3 never sends it), a
// press per toggle, so it works on both kinds of deck.

#include <QString>

class Deck;

namespace recorder {

void record(Deck& d, int seconds, const QString& wav);

} // namespace recorder
