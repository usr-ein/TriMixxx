#include "s3/wiring.h"

#include "MidiMap.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

// Control name -> canonical note. Ring pads by function, as
// mixxx_config/README.md lists them; the rest from MidiMap.hpp.
struct Named { const char* name; int note; const char* label; };
constexpr Named kControls[] = {
    {"tempo-range", midimap::PAD_A_BASE + 0, "TEMPO RANGE"},
    {"keylock", midimap::PAD_A_BASE + 1, "MASTER TEMPO"},
    {"loop8", midimap::PAD_A_BASE + 2, "8 BEAT"},
    {"loop4", midimap::PAD_A_BASE + 3, "4 BEAT"},
    {"loop-double", midimap::PAD_A_BASE + 4, "x2"},
    {"loop-halve", midimap::PAD_A_BASE + 5, "/2"},
    {"back", midimap::PAD_A_BASE + 6, "BACK"},
    {"hotcue1", midimap::PAD_B_BASE + 0, "HOT CUE 1"},
    {"hotcue2", midimap::PAD_B_BASE + 1, "HOT CUE 2"},
    {"hotcue3", midimap::PAD_B_BASE + 2, "HOT CUE 3"},
    {"hotcue4", midimap::PAD_B_BASE + 3, "HOT CUE 4"},
    {"slip", midimap::PAD_B_BASE + 4, "SLIP"},
    {"sort", midimap::PAD_B_BASE + 5, "SORT"},
    {"play", midimap::NOTE_PLAY, "PLAY"},
    {"cue", midimap::NOTE_CUE, "CUE"},
    {"loop-in", midimap::NOTE_LOOP_IN, "IN"},
    {"loop-out", midimap::NOTE_LOOP_OUT, "OUT"},
    {"reloop", midimap::NOTE_RELOOP, "RELOOP"},
    {"push", midimap::NOTE_ENC_SW, "PUSH"},
    {"jog-touch", midimap::NOTE_JOG_TOUCH, "TOUCH"},
};

} // namespace

bool Wiring::load(const QString& unitsDir, const QString& deck, QString* error) {
    m_deck = deck;
    m_buttons.clear();
    QFile f(QDir(unitsDir).filePath(deck + ".json"));
    if (!f.exists()) return true; // standard wiring
    if (!f.open(QIODevice::ReadOnly)) { *error = f.errorString(); return false; }
    QJsonParseError pe;
    QJsonObject unit = QJsonDocument::fromJson(f.readAll(), &pe).object();
    if (pe.error != QJsonParseError::NoError) { *error = f.fileName() + ": " + pe.errorString(); return false; }
    QJsonObject buttons = unit.value("buttons").toObject();
    for (auto it = buttons.begin(); it != buttons.end(); ++it)
        m_buttons[it.key().toInt(nullptr, 16)] = it.value().toString().toInt(nullptr, 16);
    m_jogReversed = unit.value("jogReversed").toBool();
    // A ring has as many nodes as its highest wired note says (trimixxx2's
    // ring A has an eighth pad, third in its chain).
    for (int phys : m_buttons) {
        if (phys < midimap::PAD_A_BASE + midimap::PAD_A_COUNT) m_ringA = qMax(m_ringA, phys - midimap::PAD_A_BASE + 1);
        else if (phys >= midimap::PAD_B_BASE && phys < midimap::PAD_B_BASE + midimap::PAD_B_COUNT)
            m_ringB = qMax(m_ringB, phys - midimap::PAD_B_BASE + 1);
    }
    return true;
}

std::optional<quint8> Wiring::note(const QString& control) const {
    for (const auto& c : kControls)
        if (control == c.name) return quint8(m_buttons.value(c.note, c.note));
    return std::nullopt;
}

QStringList Wiring::controls() {
    QStringList out;
    for (const auto& c : kControls) out << c.name;
    return out;
}

QString Wiring::padLabel(int ring, int node) const {
    int phys = (ring == 0 ? midimap::PAD_A_BASE : midimap::PAD_B_BASE) + node;
    for (const auto& c : kControls)
        if (m_buttons.value(c.note, c.note) == phys) return c.label;
    return {};
}
