#pragma once
// An emulated deck: an instance (decks/instances). Its controls go to its
// virtual S3 through the board's control socket; the screen is QEMU's.

#include "decks/deck.h"
#include "decks/instances.h"

class EmulatedDeck : public Deck {
public:
    explicit EmulatedDeck(const QString& name) : m_instance(name), m_ssh(m_instance.ssh()) {}

    QString name() const override { return m_instance.name(); }
    bool    emulated() const override { return true; }
    const SshTarget& ssh() const override { return m_ssh; }
    void    requireUp() override { m_instance.requireRunning(); }
    QString wrappers() override { return m_instance.binDir(); }
    QString controls(const QStringList& words) override;
    void    shot(const QString& png) override;

    Instance& instance() { return m_instance; }

private:
    Instance  m_instance;
    SshTarget m_ssh;
};
