#pragma once
// A deck pi-qemu acts on: an emulated one (an instance: decks/instances) or a
// real one, by its ssh alias. Both are reached over ssh, so a shell, the
// touchscreen, Mixxx's recorder and deploying work the same on both. Their
// controls differ: an emulated deck's go through its virtual S3, a real
// deck's straight into Mixxx's MIDI port -- the bytes its S3 would send, on
// its wiring, either way.

#include "util/sshtarget.h"

#include <QString>
#include <QStringList>

#include <memory>

namespace cli { class Args; }

class Deck {
public:
    virtual ~Deck() = default;

    virtual QString name() const = 0;   // as it was named: an instance's NAME, a real deck's alias
    virtual bool    emulated() const = 0;
    virtual const SshTarget& ssh() const = 0;
    // Fails, saying what to do, unless it is up: an instance running, a real
    // deck answering ssh.
    virtual void    requireUp() = 0;
    // bin/ssh and bin/scp, reaching it as `deck` (for deploy steps).
    virtual QString wrappers() = 0;

    // The S3's controls, as the control socket takes them ("press play 80",
    // "jog -120", "midi 90 7A 7F"); the text to show.
    virtual QString controls(const QStringList& words) = 0;
    // The screen, into a PNG on the Mac.
    virtual void    shot(const QString& png) = 0;

    // The hostname it runs under: what names its unit file (asked once).
    QString hostname();
    // How the person names it, for advice: NAME, or --host ALIAS.
    QString spelled() const { return emulated() ? name() : "--host " + name(); }

private:
    QString m_hostname;
};

namespace decks {

// The TARGET a verb names: --host ALIAS (a real deck), or else its first
// positional, an instance's NAME (taken from `a`).
std::unique_ptr<Deck> target(cli::Args& a);
// The options every TARGET verb takes.
QString hostOptionHelp();

} // namespace decks
