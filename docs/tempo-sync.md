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

   **Enforced, not assumed** — `ProLinkNetworkService::reconcileMastership()`,
   every poll. Our claim is ours to set and nobody else's to clear, so it used
   to outlive every way a handover can fail to reach us: a master request lost
   on the wire, a `0x27` reply the requester never heard, or a CDJ that asserts
   mastership rather than asking for it. The network settles on one master, this
   deck goes on saying it is that master, and from the booth what you see is a
   CDJ that cannot take master back — with nothing logged and no way out but a
   restart. So if another player is claiming it, we are not the master, whatever
   we think, and we stand down.

   The one exception is a handover in flight: both decks claim mastership for a
   packet or two while one names the other its successor, and a deck drops its
   claim only once the successor has picked it up. So a claim younger than
   `kMasterSettleMs` (1.5 s) is left alone, as is a rival whose `yielding_to`
   names us — otherwise a takeover would be abandoned one poll after winning it.
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

It is also **not ours**: it is `soft-takeover` in `TriMixxx.midi.xml`, which is
Mixxx's own, applied after the 14-bit fader value is assembled so it works at
full resolution. Its threshold is 3/128 of the fader travel — about a third of a
BPM at the ±6% range — so it is a crossing with a small tolerance rather than an
exact one. Writing a second implementation would have meant two of them racing
over the same fader.

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

**And the screen says where it is.** A fader connected to nothing, aimed at a
number that does not move, is a blind hunt: the DJ finds the catch by accident
and overshoots it. So while soft-takeover is holding, the tempo panel draws what
the fader is *asking for* beside what is playing — smaller and dimmer, because
it is where the hardware is and not where the deck is — and drops it the moment
the two are within Mixxx's own takeover threshold, which is the moment the fader
is really in control and the one number becomes the answer again.

The fader position reaches the screen through `[TriMixxx],tempo_fader`, which
the mapping publishes from a **second** pair of bindings on the same two CCs.
The pair that moves the deck is left exactly as it was: it is correct, and a
script binding that had to forward as well as report could only make it worse.

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

## Handing mastership over

**The grant is byte `0x9f`, not the absence of byte `0x9e`.**

A deck that presses MASTER unicasts a `0x26` at the holder and then watches the
holder's *status* for its own number to appear at byte `0x9f`. The holder keeps
claiming mastership at `0x9e` throughout, and drops that claim only once the
successor has picked it up.

We knew this from the *taking* side — it is written into
`Session::take_tempo_master`, because waiting for the holder's claim to go away
first is a deadlock — and we did the opposite on the *yielding* side. Our
handover answered the `0x26` and dropped `0x9e` on the spot, naming nobody.
From the CDJ's side that is not a grant, and it is exactly what a refusal looks
like: it asked, we went quiet, and **nobody ended up master**.

`OFF_YIELDING_TO` had been decoded since the format work and was never once
written. Both halves of the state now go on the wire together:

| | `0x9e` claim | `0x9f` successor |
|---|---|---|
| Master, nothing in flight | 1 | `0xff` |
| **Handing over** | **1** | **the requester** |
| Not master | 0 | `0xff` |

The handover completes when the requester's own status starts claiming
mastership, and `prolink-cxx` polls for that on the same cadence it already
polls a takeover. If the requester never picks it up, we let go anyway after
about two seconds and log it — holding a mastership the network is not acting on
is the worse of the two states, and is what "the CDJ cannot take master back"
looked like from the booth.

## Phase

Tempo and phase are separate problems and only the second is about beats.

While in **A**, this deck holds the master's phase for as long as SYNC is lit —
not only at the moment the button is pressed. Pressing SYNC lands on the beat;
after that the phase is held against drift, tempo nudges and anything else that
pulls the two apart.

**The correction is in track time, so it converts at the track's own tempo.**
`playposition` is a fraction of the track and the beat grid is laid out in track
time, so moving the playhead by *N* beats means moving it `N × 60 / file_bpm`
seconds — not `N × 60 / bpm`, which is the tempo being *played*. Converting at
the played tempo made every correction wrong by exactly the pitch fader: short
by 6% at ±6%, and by half at the wide range. It is the same `file_bpm` the
measurement uses to read the grid back, and the two have to agree or the deck
chases an error it is itself creating.

Correction is by moving the playhead a few tens of milliseconds, not by trimming
the tempo. An earlier version trimmed the tempo and it worked, but it rewrote the
tempo thirty times a second and the BPM readout would not sit still — which is
worse than the drift, because that number is what you read to decide whether the
decks agree.

In **B** and **C** there is no phase to hold: nothing is being followed.

## Decisions taken

Six questions the protocol does not answer and no capture settles. Each is
decided here, with the reasoning, so that a future disagreement is with the
choice rather than with an accident.

1. **SYNC on the master is inert.** It changes the published flag and nothing
   else. The alternative — that pressing it re-aligns the master to somebody —
   would mean the reference chasing its own followers.
2. **Releasing SYNC holds the tempo**, and the fader must then catch up. The
   alternative snaps the tempo back to wherever the fader was left, changing
   tempo under your hands, which is the one thing a sync button must never do.
3. **Taking master does not release SYNC.** The button stays lit and simply
   stops meaning anything, in line with decision 1. Turning it off for you would
   also change what we publish, and a flag flipping on its own is worse than one
   that is merely inert.
4. **With no master at all, the fader leads.** This happens only in the seconds
   after power-on, before anyone has claimed. A deck whose fader does nothing
   because of a master that does not exist is the worst possible reading, so
   SYNC with nobody to follow behaves exactly like SYNC off.
5. **A master with no tempo is not followed.** A stopped or empty master still
   holds mastership but publishes the no-tempo sentinel; a follower keeps
   playing what it was playing rather than following a zero to a standstill.
6. **Beats are matched; bars are not.** Beat alignment across devices is real.
   Bar alignment is not — nothing in a Mixxx beat grid names a downbeat — so a
   CDJ following us will line its bars up to an arbitrary one of ours. Chasing
   bars would drag the track by up to two beats on the strength of a guess.

## Status

Built, in two halves that do not overlap:

* **Which tempo applies** — `SyncTempo::decide()` in
  `src/network/prolink/synctempo.{h,cpp}`, called by
  `ProLinkNetworkService::followMaster()`. Every row of the table above is a
  test in `src/test/synctempo_test.cpp`, named by its state rather than its
  number so a failure says which one broke.
* **Pickup** — `soft-takeover` in the mapping, which is Mixxx's.

Not writing the tempo *is* letting the fader have it, which is why the Fader
rows are a `return` rather than a branch that does something.
