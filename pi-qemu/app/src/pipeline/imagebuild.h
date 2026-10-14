#pragma once
// A deck's dev card, built on an emulated Pi: the stock Raspberry Pi OS card
// (image/stock.env), its first-boot seed (image/user-data.in, network-config),
// then every deploy step, run as against any deck -- what
// pi_config/fresh-install.md does to a new deck, as code.
//
// It builds in an instance of its own, build-DECK (pi-qemu deck list shows it,
// deck ssh reaches it), and the card ends up in pi-qemu/.cache/build/DECK.img.
// The slow first part -- the first boot, cloud-init and the base step (apt) --
// is kept in pi-qemu/.cache/base/ and reused while its inputs are unchanged:
// the stock card, the deck's name and panel overlay, the key, the password,
// the seed and the base step. trimixxx0's build ends by remaking the golden
// pair agents' instances start from.
//
// Silent and headless the whole time.

#include <QString>
#include <QStringList>

namespace imagebuild {

struct Options {
    bool rebuildBase = false; // make the base card again even if it is cached
};

QString card(const QString& deck);      // pi-qemu/.cache/build/DECK.img
QStringList plan(const QString& deck);  // the steps, for the build window
QString build(const QString& deck, const Options& o); // the card's path

// A card pi-qemu just made, its zeros given back to the Mac's disk before
// anything clones it (files::sparsify), said in a line. One it can't make
// sparse stays as it was, with a warning: the build goes on.
void sparse(const QString& card);

} // namespace imagebuild
