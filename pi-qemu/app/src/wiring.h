#pragma once
// Which note each control sends, on a given deck. Canonical notes come from
// the firmware's MidiMap.hpp; a deck wired in a different order has a
// mixxx_config/units/<deck>.json saying where each control actually lands --
// the same file apply.py and deck-poke read.

#include <QHash>
#include <QString>
#include <QStringList>
#include <optional>

class Wiring {
public:
    // Load the unit file for `deck`, if there is one. No file: standard wiring.
    bool load(const QString& unitsDir, const QString& deck, QString* error);

    QString deck() const { return m_deck; }
    // The note the S3 sends for a named control ("play", "back", "hotcue1").
    std::optional<quint8> note(const QString& control) const;
    // Every control name, for help text and the CLI.
    static QStringList controls();
    // What a ring pad does on this deck, for its label ("BACK", or "" if unbound).
    QString padLabel(int ring, int node) const;
    int ringSize(int ring) const { return ring == 0 ? m_ringA : m_ringB; }
    bool jogReversed() const { return m_jogReversed; }

private:
    QString           m_deck;
    QHash<int, int>   m_buttons; // canonical note -> physical note
    int               m_ringA = 7, m_ringB = 6;
    bool              m_jogReversed = false;
};
