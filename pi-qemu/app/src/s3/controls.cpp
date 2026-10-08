#include "s3/controls.h"

#include "s3/protocol.h"
#include "util/fail.h"

namespace s3 {

namespace {

quint8 noteOf(const Wiring& wiring, const QString& control) {
    const auto n = wiring.note(control);
    if (!n) fail("no control called " + control + "; try: " + Wiring::controls().join(' '), 2);
    return *n;
}

int number(const QString& word, const QString& what) {
    bool ok = false;
    const int n = word.toInt(&ok);
    if (!ok) fail(what + " is a number, not " + word, 2);
    return n;
}

} // namespace

std::optional<Sequence> sequence(const QStringList& words, const Wiring& wiring) {
    if (words.isEmpty()) return std::nullopt;
    const QString cmd = words[0];
    const QStringList w = words.mid(1);
    auto need = [&](int n, const QString& usage) {
        if (w.size() < n) fail(cmd + " " + usage, 2);
    };
    Sequence s;
    if (cmd == "press") {
        need(1, "CONTROL [MS]");
        const quint8 n = noteOf(wiring, w[0]);
        const int ms = w.size() > 1 ? number(QString(w[1]).remove("ms"), "MS") : 80;
        s.steps = {{button(n, true), qMax(ms, 1)}, {button(n, false), 0}};
        s.said = QString("pressed %1 for %2 ms").arg(w[0]).arg(ms);
    } else if (cmd == "down" || cmd == "up") {
        need(1, "CONTROL");
        s.steps = {{button(noteOf(wiring, w[0]), cmd == "down"), 0}};
        s.said = cmd + " " + w[0];
    } else if (cmd == "jog") {
        need(1, "TICKS");
        const int t = number(w[0], "TICKS");
        s.steps = {{jog(wiring.jogReversed() ? -t : t), 0}}; // as this deck's encoder is wired
        s.said = QString("jog %1").arg(t);
    } else if (cmd == "touch") {
        need(1, "on|off");
        if (w[0] != "on" && w[0] != "off") fail("touch on|off", 2);
        s.steps = {{button(noteOf(wiring, "jog-touch"), w[0] == "on"), 0}};
        s.said = "touch " + w[0];
    } else if (cmd == "browse") {
        need(1, "N");
        s.steps = {{encoder(number(w[0], "N")), 0}};
        s.said = "browse " + w[0];
    } else if (cmd == "tempo") {
        need(1, "VALUE|center");
        const int v = w[0] == "center" ? 8192 : number(w[0], "VALUE");
        if (v < 0 || v > 16383) fail("tempo is 0..16383 (8192, or center, in the middle)", 2);
        s.steps = {{tempo(v), 0}};
        s.said = QString("tempo %1").arg(v);
    } else if (cmd == "midi") {
        need(1, "HEX...");
        QByteArray b;
        if (!parseHex(w, &b)) fail("midi takes bytes in hex: midi 90 3C 7F", 2);
        s.steps = {{b, 0}};
        s.said = QString("sent %1 bytes").arg(b.size());
    } else {
        return std::nullopt;
    }
    return s;
}

QString controlsHelp() {
    return QStringLiteral(
        "  press CONTROL [MS]     press and release (default 80 ms)\n"
        "  down CONTROL | up CONTROL\n"
        "  jog TICKS              turn the platter; + is clockwise, 12960 ticks per turn\n"
        "  touch on|off           the platter's touch sensor (scratch)\n"
        "  browse N               the track encoder; + is up\n"
        "  tempo VALUE            the fader: 0..16383, 8192 or 'center' in the middle\n"
        "  midi HEX...            raw bytes, e.g. midi 90 3C 7F\n"
        "Controls: ") + Wiring::controls().join(' ');
}

} // namespace s3
