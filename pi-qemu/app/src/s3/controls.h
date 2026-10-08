#pragma once
// The deck's controls in words, as `pi-qemu deck press|down|up|jog|touch|
// browse|tempo|midi` and the control socket take them, turned into what the
// S3 on a given wiring sends: MIDI bytes, and how long to hold before the
// next ones. Both kinds of deck play the same sequence: an emulated one on its
// virtual S3's UART, a real one into Mixxx's MIDI port.

#include "s3/wiring.h"

#include <QByteArray>
#include <QStringList>
#include <QVector>

#include <optional>

namespace s3 {

struct Step {
    QByteArray bytes;
    int        holdMs = 0; // before the next step
};

struct Sequence {
    QVector<Step> steps;
    QString       said; // what was done, for the person: "pressed play for 80 ms"
};

// nullopt if words[0] is not one of the controls (the caller may know it:
// leds, stick, ...). Fails, status 2, on a control's bad arguments.
std::optional<Sequence> sequence(const QStringList& words, const Wiring& wiring);

// The help lines for the control words.
QString controlsHelp();

} // namespace s3
