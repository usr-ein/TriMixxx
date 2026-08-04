# Tempo, master and beat sync

What this deck does about tempo when there is a CDJ on the other side of the
booth. Written to be argued with before it is built: every row below is a claim
about correct behaviour, and the ones I am least sure of are marked.

Scope is deliberately two devices — this deck and one CDJ. Three or more players
change nothing about the rules but a great deal about how many states there are
to write down.

## Invariants

1. **Exactly one device is tempo master at any moment.** Never none, never two.
   Mastership moves by request: a deck asks the holder to hand over, the holder
   answers and stands down.
2. **BEAT SYNC is per-deck and independent.** Neither, either or both may have
   it engaged.
3. **A deck's tempo is its track's tempo times its pitch fader.** Both halves go
   on the wire — bytes `0x92` and `0x8c` of a status packet — and every other
   player multiplies them. A deck whose two halves disagree is meaningless.

## The one thing that collapses the table

> **A deck's own BEAT SYNC only matters while that deck is not master.**

The master is the reference. It cannot follow itself, so SYNC on the master is
inert: it changes the flag published on the wire and nothing else. That is what
turns eight states into three behaviours for this deck:

| | Behaviour |
|---|---|
| **A** | Not master, SYNC on — follow the master's tempo *and* phase. This deck's fader is decoupled. |
| **B** | Not master, SYNC off — free. This deck's own fader, ignoring everyone. |
| **C** | Master — this deck's own fader, subject to pickup (below). Our SYNC flag is inert. |

The CDJ's SYNC state never changes what *we* do. It decides whether the CDJ
follows us, which is its business. All we can do is publish a tempo it can
follow.

## The eight states

Your table had seven rows. With one master and two independent SYNC switches
there are eight, and the missing one — **row 4** — is the most common live case:
the other deck is simply playing, not synced to anything, and we beat-match to
it. It is every test run against the hardware so far.

| # | CDJ master | CDJ sync | TriMixxx master | TriMixxx sync | This deck does | The CDJ does |
|---|---|---|---|---|---|---|
| 1 | Y | N | N | N | **B** — own fader | own fader |
| 2 | Y | Y | N | N | **B** — own fader | own fader (its SYNC is inert) |
| 3 | Y | Y | N | Y | **A** — follows the CDJ | own fader (its SYNC is inert) |
| 4 | Y | N | N | Y | **A** — follows the CDJ | own fader |
| 5 | N | N | Y | Y | **C** — own fader, pickup; our SYNC inert | own fader, ignores us |
| 6 | N | N | Y | N | **C** — own fader, pickup | own fader, ignores us |
| 7 | N | Y | Y | N | **C** — own fader, pickup | **follows us** |
| 8 | N | Y | Y | Y | **C** — own fader, pickup; our SYNC inert | **follows us** |

Rows 1 and 2 are identical from this deck. So are 3 and 4. So are 5, 6, 7 and 8,
apart from which flag we publish.

## Pickup

Pickup is **not a property of any row above**. It is a property of how the deck
arrived there, which is why the transitions matter more than the states.

While this deck follows a master, its tempo comes from the wire and the fader
under your hand is connected to nothing. The two drift apart — the master drags
the deck from 130 to 140 while the fader still sits at 130 — and the moment this
deck stops following, obeying the fader would drop ten BPM with nobody having
touched it.

So:

> The fader does nothing until it **crosses** the tempo actually playing, and
> only then leads.

In whichever direction. Left below the playing tempo, it catches coming up; left
above, it catches coming down. It is a crossing, not a proximity: at 139.9
against 140 nothing happens.

### When the pickup is armed

| Transition | Tempo | Fader |
|---|---|---|
| A → C (we take master while following) | stays where the master left it | armed; must cross |
| A → B (SYNC released while following) | stays where the master left it | armed; must cross |
| B → A, C → A (we start following) | jumps to the master's | decoupled; the side it is on is remembered |
| C → B (mastership lost, SYNC off) | unchanged — we were never following | still in control |
| B → C, C → B with the fader already in control | unchanged | still in control |
| A track is loaded, or the deck emptied | — | released; the next track starts with a live fader |

The worked example from your description, as one session:

1. CDJ master at 130, we follow. Our fader sits at 130.
2. CDJ runs up to 140. We follow to 140. **Our fader has not moved: still 130.**
3. We take master. Tempo stays 140. Fader is below.
4. Fader to 133 — nothing. To 139.9 — nothing.
5. Fader reaches 140 — caught. From here the fader is the tempo.
6. Fader to 143, then down to 120 — both go out, and a synced CDJ follows.
7. CDJ takes master back at 120. Its own fader was left at 140, so from its side
   the fader is now above and must come down: 130 does nothing, 120 catches.

## Phase

Tempo and phase are separate problems and only the second is about beats.

While in **A**, this deck holds the master's phase for as long as SYNC is lit —
not only at the moment the button is pressed. Pressing SYNC lands on the beat;
after that the phase is held against drift, tempo nudges and anything else that
pulls the two apart.

Correction is by moving the playhead a few tens of milliseconds, not by trimming
the tempo. An earlier version trimmed the tempo and it worked, but it rewrote the
tempo thirty times a second and the BPM readout would not sit still — which is
worse than the drift, because that number is what you read to decide whether the
decks agree.

In **B** and **C** there is no phase to hold: nothing is being followed.

## What I am unsure of — please correct

1. **Is SYNC on the master really inert?** I have assumed pressing SYNC on the
   master deck changes nothing but the published flag. If a CDJ does something
   else — re-aligns to the previous master, say — rows 5 and 8 are wrong.
2. **Does releasing SYNC hold the tempo?** I have assumed the deck keeps playing
   what it was playing and the fader must catch up. The alternative — snapping
   back to the fader position — changes tempo under your hands, so I doubt it,
   but it is worth confirming.
3. **Does taking master release SYNC?** I have assumed not: the button stays lit
   and simply stops meaning anything.
4. **What is master at power-on**, before anyone has pressed anything? Invariant
   1 says someone must be. Today this deck never claims mastership unless you
   press MASTER, so if the CDJ does not claim it either, there is briefly no
   master at all — and a deck with SYNC lit has nothing to follow. I have
   assumed that in that state the fader simply leads, rather than the deck
   freezing.
5. **A master with no track loaded, or stopped.** It still holds mastership, but
   it publishes no tempo. I have assumed a follower keeps its own tempo rather
   than following a zero.
6. **Bar alignment.** Beat alignment across devices is real. Bar alignment is
   not: nothing in a Mixxx beat grid names a downbeat, so this deck matches
   beats and does not attempt to match bars. A CDJ following us will line its
   bars up to an arbitrary one of ours.

## Status

The pickup rules above are implemented and tested as a pure unit
(`src/network/prolink/synctempo.{h,cpp}`, `src/test/synctempo_test.cpp`) — that
was written before this document, which was the wrong order.

**It is not connected to the deck.** It needs the *hardware* fader position, and
Mixxx has no such concept: writing `bpm` moves `rate`, so `rate` is where the
tempo is, not where your hand is. The real position is visible only to the
controller mapping, which sees the raw 14-bit CC. Wiring it up therefore means
`TriMixxx.scripts.js` taking the tempo fader, publishing the hardware position,
and stopping the direct binding to `rate`.
