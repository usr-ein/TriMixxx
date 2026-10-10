#pragma once
// An emulated CDJ-2000NXS's panel, as the emulator's control channel takes
// it: every key `cdj press` knows, by the firmware's own names
// (cdj2000-emulator's tools/cdj_main/nxs_panel.py), with the contact it
// closes in the panel's payload; and the lamps, as the channel's `state`
// reports what MAIN last lit (emulator/qemu/cdj2000_lamps.c names them).

#include <QHash>
#include <QString>
#include <QVector>

namespace cdj {

struct Key {
    QString name;     // the firmware's: "PLAY", "HOT CUE A", "INFORMATION"
    int     byte = 0; // the contact: a bit of the panel's payload
    quint8  mask = 0;
};

// Every key `cdj press` knows, in nxs_panel.py's order.
const QVector<Key>& keys();

// A key by any name `cdj press` takes: the firmware's in any case, with -
// or _ for spaces ("hot-cue-a"), and "enter" (the encoder's push), "back"
// (RETURN) and "info". Null if there is none.
const Key* key(const QString& name);

// The control channel's lines: a contact held and let go, the browse
// encoder turned (a signed count of detents; analogue field 7), and the
// lamps asked for.
QString downLine(const Key& k);
QString upLine(const Key& k);
QString rotaryLine(int detents);
inline constexpr char kStateLine[] = "state";

// The lamps lit in a `state` reply ("ok state ... lamps=CUE:on,SOURCE_SD:3
// ..."), by name: "on", "blink", or a two-bit lamp's level. The dark ones
// are not there.
QHash<QString, QString> lampsOf(const QString& stateReply);

// What a lamp shows: two-bit lamps are 1 when their key is available (a dim
// backlight) and 3 when it is on; one blinking goes between the two.
enum class Lit { Off, Dim, On, Blink };
Lit lit(const QHash<QString, QString>& lamps, const QString& lamp);
bool twoBit(const QString& lamp); // the sources, the browse keys, LOOP MODE, SYNC, MASTER

} // namespace cdj
