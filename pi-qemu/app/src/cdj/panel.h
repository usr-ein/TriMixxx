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

// The TEMPO slider, as the NXS takes it through analogue fields 2 (its
// position) and 3 (its centre), 0..65535: the tempo is worked out again when
// the position changes, against the centre, which goes first. With the
// centre at 32768, 0 is the slider's minus end and 65535 its plus end
// (-/+10 % on the 10 % range). Both fields start at 0 on the NXS.
inline constexpr int kTempoCentre = 32768;
QString tempoCentreLine();
QString tempoLine(int position);

// A `state` reply ("ok state frames=.. held=.. a2=-0/0 .. lamps=.. ..") as
// its fields; empty if it is not one.
QHash<QString, QString> stateOf(const QString& stateReply);

// The lamps lit in a `state` reply ("ok state ... lamps=CUE:on,SOURCE_SD:3
// ..."), by name: "on", "blink", or a two-bit lamp's level. The dark ones
// are not there.
QHash<QString, QString> lampsOf(const QString& stateReply);

// An analogue field as `state` reports it ("a2=32768/32768", "-" in front
// when nobody drives it): its target, or -1 when it is not driven.
int analogTarget(const QHash<QString, QString>& state, int field);

// The DIRECTION lever (contact 15.1, active low) as `state`'s literal levels
// have it: true at REV.
bool leverAtRev(const QHash<QString, QString>& state);

// What a lamp shows: two-bit lamps are 1 when their key is available (a dim
// backlight) and 3 when it is on; one blinking goes between the two.
enum class Lit { Off, Dim, On, Blink };
Lit lit(const QHash<QString, QString>& lamps, const QString& lamp);
bool twoBit(const QString& lamp); // the sources, the browse keys, LOOP MODE, SYNC, MASTER

} // namespace cdj
