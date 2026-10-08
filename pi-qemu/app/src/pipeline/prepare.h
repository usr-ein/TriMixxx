#pragma once
// A deck readied for its first release card, from the system it runs today
// (pi-qemu/PLAN.md §7), before its card is made and flashed. It only reads
// the deck, unless `eeprom`:
//  1. its board's serial goes into mixxx_config/units/DECK.json ("serial";
//     the file is made if there is none), so the release's config.txt gives
//     this board a section of its own, its panel overlay in it
//     (release/boot/deck-sections.py). Build the release after this, with the
//     unit file committed.
//  2. its ssh host keys become its identity's (pi-qemu/.cache/decks/DECK/ssh),
//     unless that has keys already: the card keeps them, and known_hosts
//     stays right through the reflash.
//  3. its bootloader: one from before 2022-12-01 doesn't know tryboot_a_b and
//     would start a fresh card's empty slot B (phase 1, T0). Without `eeprom`
//     that fails the run.
//  4. its boot files, EEPROM config, NetworkManager profiles and ALSA state
//     are kept in pi-qemu/.cache/captures/DECK-<time>/ (Wi-Fi passwords
//     included: gitignored, readable by its owner only), and the lines of its
//     config.txt a unit file may need are shown: a panel's dtoverlay goes into
//     "panelOverlay", other screen lines into "configTxt".
//
// `eeprom` updates the bootloader and gives it its boot watchdog (PLAN.md
// invariant 14: BOOT_WATCHDOG_TIMEOUT=45, BOOT_WATCHDOG_PARTITION=2), once the
// firmware's log shows Linux starting well within 45 s: `rpi-eeprom-config
// --apply` writes the config into the newest bootloader the deck's own
// rpi-eeprom package has, flashed at the next start from the card in the deck
// now. It reboots the deck, then checks the version and the config. On an A/B
// card the update goes to p1, where the boot ROM looks. The person's call.

#include <QString>

class Deck;

namespace prepare {

// Its exit status: 1 if the deck is not ready yet (a bootloader too old).
int prepare(Deck& d, const QString& deck, bool eeprom);

// The unit file's text with "serial" set to `serial`, its keys in their order
// (a new one goes last); a new file if `text` is empty.
QString withSerial(const QString& text, const QString& deck, const QString& serial);
// config.txt's lines that matter for comparing two cards: up to the lines a
// release adds, without comments, blank lines and [all].
QStringList configLines(const QString& configTxt);

} // namespace prepare
