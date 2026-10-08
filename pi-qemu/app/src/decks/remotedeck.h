#pragma once
// A real deck, by its ssh alias. Its controls go straight into the MIDI port
// Mixxx opened for the deck's controller, with aseqsend on the deck: the
// bytes its S3 would send, on its wiring (mixxx_config/units/<hostname>.json),
// so they run the real mapping and scripts -- the whole chain bar the copper.
// Its screen comes from scrot, on the deck's X display.

#include "decks/deck.h"
#include "s3/wiring.h"

#include <QTemporaryDir>

#include <optional>

class RemoteDeck : public Deck {
public:
    explicit RemoteDeck(const QString& alias) : m_ssh{alias, {}} {}

    QString name() const override { return m_ssh.alias; }
    bool    emulated() const override { return false; }
    const SshTarget& ssh() const override { return m_ssh; }
    void    requireUp() override;
    QString wrappers() override;
    QString controls(const QStringList& words) override;
    void    shot(const QString& png) override;

private:
    const Wiring& wiring();

    SshTarget                      m_ssh;
    std::optional<QTemporaryDir>   m_bin;
    std::optional<Wiring>          m_wiring;
    bool                           m_up = false;
};
