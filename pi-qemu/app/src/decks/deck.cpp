#include "decks/deck.h"

#include "cli/args.h"
#include "decks/emulateddeck.h"
#include "decks/remotedeck.h"
#include "util/fail.h"

QString Deck::hostname() {
    if (m_hostname.isEmpty()) {
        proc::Options o;
        o.timeoutMs = 30000;
        const proc::Result r = ssh().capture("hostname", o);
        if (!r.ok() || r.text().isEmpty()) fail("cannot reach " + name() + " over ssh");
        m_hostname = r.text();
    }
    return m_hostname;
}

namespace decks {

std::unique_ptr<Deck> target(cli::Args& a) {
    if (a.has("host")) return std::make_unique<RemoteDeck>(a.value("host"));
    return std::make_unique<EmulatedDeck>(a.take("TARGET: an emulated deck's NAME, or --host ALIAS"));
}

QString hostOptionHelp() {
    return "a real deck, by its ssh alias (trimixxx-pi), instead of an emulated deck's NAME";
}

} // namespace decks
