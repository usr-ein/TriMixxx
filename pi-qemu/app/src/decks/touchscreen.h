#pragma once
// The deck's touchscreen, from the Mac: xdotool on the deck's X display,
// over ssh, on either kind of deck. The panel is an X11 touch device and Qt
// gets its taps as synthesised pointer events, which is what xdotool makes,
// so this is a faithful touch. What it cannot make is a hover -- and must
// not: the menu bar's reveal is gated on hovering, which a finger can't do.

#include <QStringList>

class Deck;

namespace touchscreen {

void tap(Deck& d, int x, int y);
void longpress(Deck& d, int x, int y, int ms);
// A stepped drag, so a gesture recogniser sees movement and a kinetic
// scroller gets a velocity: swipe (BACK, left to right) and flick (scroll)
// differ only in their speed.
void drag(Deck& d, int x1, int y1, int x2, int y2, int ms);
void keys(Deck& d, const QStringList& keysyms);
QString where(Deck& d); // the pointer's position

} // namespace touchscreen
