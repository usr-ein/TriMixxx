#pragma once
// What code a card holds: the commit of the checkout it was built from, and
// whether that checkout held exactly that commit's code for the deck --
// nothing changed, nothing untracked, its submodules where the commit points
// them -- in the paths that go onto a deck (deckPaths). An image build keeps
// it beside its card (.cache/build/DECK.json), the golden its own
// (.cache/golden/trimixxx0.json), and each emulated deck the golden's it
// started from (decks/instances): enough to tell whether a deck runs main's
// latest (pipeline/golden).

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace source {

// The paths whose content goes onto a deck's card: what image build and the
// deploy steps read (the submodules mixxx, its lib/prolink, and
// mixxx_config/ttymidi with them), and image build itself, which seeds,
// sets up and seals the card. Two commits that agree there make the same
// deck.
QStringList deckPaths();

struct Record {
    QString commit;        // the checkout's HEAD when the card was built
    bool    clean = false; // the checkout held exactly that commit's code for the deck
    QString why;           // when not clean: what differed
    QString date;          // the commit's, ISO 8601
    QString subject;       // the commit's
    QString checkout;      // the checkout it was built from
    QString built;         // when the card was built, ISO 8601 UTC
    QString made;          // a golden's: when it was made from the card
    QString card;          // the card a golden was made from; a deck's own card (up --from, a build's)

    bool empty() const { return commit.isEmpty() && card.isEmpty(); } // no record at all
    bool known() const { return clean && !commit.isEmpty(); }         // a commit's code, exactly
    // "5f0b01a of 2026-10-17 12:40", with "+ changes (...)" when not clean;
    // a card's path when there is no commit.
    QString describe() const;
    QJsonObject json() const;
    static Record fromJson(const QJsonObject& o);
};

Record read(const QString& file); // empty if there is none, or it can't be read
void   write(const QString& file, const Record& r); // whole: a new file renamed over the old

// The record beside a card: foo.img's is foo.json.
QString beside(const QString& card);

// What `checkout` holds now: its HEAD, and whether its code for the deck is HEAD's.
Record of(const QString& checkout);
// Empty when `checkout`'s code for the deck -- its working tree, untracked
// files and submodules included -- is exactly `commit`'s; otherwise what differs.
QString differs(const QString& checkout, const QString& commit);
// Whether commits a and b of the repository at `repo` make the same deck;
// false if either is unknown there.
bool same(const QString& repo, const QString& a, const QString& b);
// Whether two goldens' records hold the same deck: by their commits when
// both know theirs (same), else only when they are the same making of it.
bool sameGolden(const QString& repo, const Record& a, const Record& b);

} // namespace source
