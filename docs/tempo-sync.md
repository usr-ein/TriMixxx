# Tempo, master and beat sync

What this deck does about tempo when there is a CDJ on the other side of the
booth. Written to be argued with before it is built: every row below is a claim
about correct behaviour.

**Revised after the owner's review of 2026-10-04.** Seventeen rulings, **D1–D17**
below, settled what the first version left open or got wrong, and they win
wherever the two disagree. They are listed at the end; KEY SYNC's three (D7, D8,
D12) are described in `mixxx_config/README.md`.

The tables assume two devices, this deck and one CDJ. Three or more players
change nothing about the rules, but a great deal about how many states there
are to write down. Where a rule has to pick one deck out of several — what SYNC
follows when nobody is master, who gets master when it is handed on — the
section says how.

## Invariants

1. **At most one device is tempo master, and never none for long.** Mastership
   moves by request: a deck asks the holder to hand over, and the holder names
   it as its successor and then stands down. A master can also hand over unasked
   (D13). If the network loses its master, a deck that is playing and following
   nobody claims it after a few seconds (D2, D3; see *Taking it unasked*).

   **Enforced, not assumed** — `ProLinkSync::reconcileMastership()`,
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
   A claim we made unasked is different: it only ever filled an empty
   mastership, so it yields at once to any rival.

   **Only claims we can still hear count.** A deck that has gone (cable pulled,
   powered off) keeps its last status, mastership included, until it is
   forgotten some 30 s later. A status older than a second is not believed.
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

**What A follows** (D1) is chosen every poll by `chooseSyncSource()`:

* **The master, whenever there is one.** A real master always wins, even over a
  lower-numbered deck that is playing. While it is handing master on, nothing is
  followed for the packet or two that takes.
* **With no master**, the lowest-numbered deck that is playing — never a deck
  that is itself synced and numbered above us, or two TriMixxx decks with SYNC
  lit would follow each other and neither could ever take master.
* **Nobody playing and no master**: nothing to follow, and the fader leads,
  exactly as with SYNC off.
* **The master stops while we follow it** (D15): SYNC lets go and goes dark,
  the tempo it was following carries on as our own, and the fader has to catch
  up with it. Following on would mean following whatever that master does while
  nobody can hear it — a new track, its pitch fader moved — and jumping to it on
  air when it plays again. The deck follows again when the DJ presses SYNC.

## The eight states

With one master and two independent SYNC switches there are eight, and **row
4** is the most common live case: the other deck is simply playing, not synced
to anything, and we beat-match to it.

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

**Big tempo gaps are followed literally** (D17): a master at 70 is followed at
70, not at 140. Half and double tempo are a DJ's call, not SYNC's.

## Pickup

Pickup is **not a property of any row above**. It is a property of how the deck
arrived there, which is why the transitions matter more than the states.

While this deck follows a master, its tempo comes from the wire and the fader
under your hand is connected to nothing. The two drift apart — the master drags
the deck from 130 to 140 while the fader still sits at 130 — and the moment this
deck stops following, obeying the fader would drop ten BPM with nobody having
touched it.

So:

> The fader does nothing until it **crosses** the tempo actually playing, or
> comes within 0.05 BPM of it (D10), and only then leads.

In whichever direction. Left below the playing tempo, it catches coming up; left
above, it catches coming down. A crossing is two consecutive fader positions on
either side of the tempo, so a fast sweep through it still catches.

**It is the mapping's, not Mixxx's soft-takeover.** It used to be
`<soft-takeover/>` on the XML binding, which knows nothing about SYNC: a fader
moved across the followed tempo was let through, and the fader and SYNC then
fought over the tempo thirty times a second. And soft-takeover's catch is a
proximity of 3/128 of the travel — 0.36 BPM at ±6%, 6 BPM at WIDE — with the
tempo jumping by that much when it caught. The script now assembles the 14-bit
fader itself, writes `rate` from it, and writes nothing while
`[ProLink],following` says SYNC follows a deck (`TriMixxx.scripts.js`, "Tempo
fader").

**And the screen says where it is.** A fader connected to nothing, aimed at a
number that does not move, is a blind hunt: the DJ finds the catch by accident
and overshoots it. So the tempo panel draws what the fader is *asking for*
beside what is playing — smaller and dimmer, because it is where the hardware
is and not where the deck is — whenever the fader has not caught the tempo, in
**B** and **C**, and never in **A**, where the fader is connected to nothing and
where it sits is not a target (D6). The script publishes the fader as
`[TriMixxx],tempo_fader` from the same number it writes to `rate`, so the panel
says "caught" by comparing the two rather than modelling a threshold.

### When the pickup is armed

| Transition | Tempo | Fader |
|---|---|---|
| A → C (we take master while following) | stays where the master left it | armed; must cross |
| A → B (SYNC released while following) | stays where the master left it | armed; must cross |
| A → B because the master stopped (D15) | stays where the master left it | armed; must cross |
| B → A, C → A (we start following) | jumps to the master's | decoupled |
| C → B (mastership lost, SYNC off) | unchanged — we were never following | still in control |
| B → C, C → B with the fader already in control | unchanged | still in control |
| Range changed on A1 (D11) | unchanged | armed; must cross at its new position |
| A track is loaded while not following (D5) | the fader's tempo | in control |

**While following, the range widens to hold the tempo** (D9). A tempo the fader
cannot reach at its range would only ever be caught at the end stop, with a drop
of several BPM; so the range steps up to the smallest one that holds it. Mixxx
keeps the tempo on a range change, so nothing is heard. A1 skips, for the same
reason, any range too narrow for the tempo playing (D11).

**A track loaded while not following starts at the fader** (D5): the tempo is
where the fader is, and the fader leads. The S3 sends the fader only when it
moves, so after a boot the mapping asks it where it is (SysEx `0x04`, see
`firmwares/trimixxx-midi/CLAUDE.md`). While following, SYNC sets the new
track's tempo.

The worked example, as one session:

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

Both halves of the state go on the wire together:

| | `0x9e` claim | `0x9f` successor |
|---|---|---|
| Master, nothing in flight | 1 | `0xff` |
| **Handing over** | **1** | **the successor** |
| Not master | 0 | `0xff` |

**Yielding.** We answer a `0x26` by naming the requester at `0x9f`, keep
claiming, and let go once the requester's own status claims mastership. The
status naming it goes out at once, as a CDJ's does, not on the next 200 ms
tick (prolink 0.4): a CDJ asking claims on that packet, and a hand-over that
took us 284-373 ms takes a CDJ 67-133 ms (S28). If it
never picks it up, we let go anyway after about two seconds and log it —
holding a mastership the network is not acting on is the worse of the two
states, and is what "the CDJ cannot take master back" looked like from the
booth.

**Taking** (MASTER pressed on this deck) waits for that grant rather than for
the holder's claim to go away, which never happens first. One take is in flight
at a time, the request is sent once more if no grant comes, and a holder we can
no longer hear is not asked: with nobody holding it, we just claim it.

### Without being asked (D13)

A CDJ hands mastership over on its own when it stops while a synced deck plays
on, and this deck now does the same in both directions:

* **Taking it when handed.** A master naming us at `0x9f` without our having
  asked — a CDJ master stopping while our synced deck plays — is taken up.
* **Handing it on when we stop.** While our deck is stopped and we are master,
  we name the lowest-numbered deck that plays (its status flag `0x40`) as our
  successor, and a synced one only if we are synced. That is what a
  CDJ-2000NXS does: a paused master hands over to a deck that plays, unless it
  is synced and that deck is not (S28 193.655 and 208.276; E01, E05, E06 A-E).
  The rule holds while we stay stopped, so a deck that starts later, or our
  SYNC going off, gets it too. But master is never offered twice to one deck in
  one stop. If nobody picks it up we keep master: an empty mastership is worse.
  The cases without sync rest on emulated CDJs only.

### Taking it unasked (D2, D3)

**The first deck to play takes master at once.** With nobody master, our deck
starting to play claims it on the spot, as a CDJ does (S28 22.058, E01 23.269).
It does so only when every player on the network has been heard, so that
"nobody is master" is known rather than merely unheard.

Otherwise, when the network has had no master for a while, this deck claims it —
if it is playing a track with a tempo, is **not** following another deck
through SYNC, and has not stood down or handed over in the last 10 s (it must
not take back what it just gave away).

The wait is 3 s — doubled after every collision, up to four of them — plus 1 s
per player number above 1. The per-number step is the tie-break: two TriMixxx decks that lose
the master together both wait, and the lower-numbered one's claim is on the wire
a second before the other's deadline, which then sees a master and does not
claim. If packet loss makes both claim anyway, both yield (an unasked claim
yields to any rival) and wait twice as long, in the same order. The rules are
`automaster::mayClaim()` and `claimDelayMs()`.

## Phase

Tempo and phase are separate problems and only the second is about beats.

While in **A**, this deck holds the phase of the deck it follows for as long as
SYNC is lit — not only at the moment the button is pressed. Pressing SYNC lands
on the beat; after that the phase is held against drift, tempo nudges and
anything else that pulls the two apart. How (D16):

* **Measured as heard.** Our side is where the DAC is, not where the engine has
  got to, which is a buffer or two ahead; the other side is the followed deck's
  place in its bar, from its beat packets. What Mixxx cannot know — the USB
  codec, and a CDJ's own gap between its beat packet and its sound — is the
  persistent `[ProLink],phase_trim_ms`, measured once per rig (see
  `mixxx_config/README.md`).
* **Only against fresh beats.** A deck whose beats have stopped coming, or whose
  next beat is overdue, has a phase standing at the end of its last beat; it is
  not measured against.
* **Decided on half a second, not one sample.** One sample is a poor witness: up
  to 15 polls are kept and the median decides. A single sample over the
  threshold used to trigger a seek of its own size, turning noise into a real
  error that was corrected back 1.5 s later.
* **Landing is one exact seek**, with Mixxx's own `beatjump` by the error in
  beats — when SYNC is pressed, the deck starts, or the DJ lets go of it. The
  jump works on the deck's grid, so converting beats to track time is Mixxx's,
  and quantize does not touch it (a `playposition` write was snapped back to
  where it started, so for a while SYNC never landed at all).
* **Holding is a trim, not a seek.** A seek mid-mix is a flam of its own, so a
  small error is closed by playing up to 1% fast or slow for a couple of
  seconds — inside the engine (`[ChannelN],phase_trim`), where the BPM read-out
  never sees it. An earlier version trimmed `bpm` itself, and the read-out
  jittered around the master's value for as long as SYNC was lit, which is worse
  than the drift: that number is what a DJ reads to decide whether the decks
  agree. Under 0.003 beat nothing is done; a slip over 0.1 beat (a jog jump on
  the master, a missed landing) gets a seek instead, at most every 1.5 s.
* **Paused while the DJ has the deck** (D4): jog touched or bent (until 0.6 s
  after the last bend), scratching, in a loop, in slip, in reverse, or not
  playing — a cue held as a preview included. The deck is landed again 150 ms
  after it is let go, once the engine has done what the release itself queued.
* **Beats, not bars.** The error is wrapped to half a beat either way, so the
  playhead never moves further than that. Which beat starts a bar is the next
  section.

In **B** and **C** there is no phase to hold: nothing is being followed.

## Bars

**Bars count from rekordbox's downbeat** (D14): the first downbeat of a
rekordbox track arrives as its intro cue, and our bars start there — or on the
first beat of the grid when there is none. Our place in the bar is read off the
track's own beat grid at the audible position (`AudibleBeatClock`), so a
variable-tempo grid does not drift either. It is what the phase meter's bottom
row draws and the beat-in-bar a CDJ syncing to us is told.

SYNC still corrects only to the nearest beat: lining bars up across decks would
drag the track by up to two beats, and that is the DJ's job.

## The phase meter

Two rows: the top one is the deck this one is mixing against, the bottom one is
us. Not always the same deck SYNC follows (`chooseMeterDeck()`):

* **The master first, playing or not**, then any other deck playing, then any
  deck with a place on its grid at all — one being cued up is still the deck the
  next mix lines up against. Ties go to the lowest number.
* **Drawn from its beats while they arrive**, and held where its status puts it
  (to the beat) otherwise, drawn dim. A paused deck the DJ winds back moves a
  beat at a time.
* **Only decks still heard**, as for mastership.
* The top row reads **"M3"** for player 3 holding master, **"3"** for a deck
  drawn because nobody is master; our row reads **"M"** when we hold it.

## Decisions taken

Questions the protocol does not answer and no capture settles, each decided
here so that a future disagreement is with the choice rather than with an
accident.

1. **SYNC on the master is inert.** It changes the published flag and nothing
   else. The alternative — that pressing it re-aligns the master to somebody —
   would mean the reference chasing its own followers.
2. **Releasing SYNC holds the tempo**, and the fader must then catch up. The
   alternative snaps the tempo back to wherever the fader was left, changing
   tempo under your hands, which is the one thing a sync button must never do.
3. **Taking master does not release SYNC.** The button stays lit and simply
   stops meaning anything, in line with decision 1. Turning it off for you would
   also change what we publish, and a flag flipping on its own is worse than one
   that is merely inert. (The one exception is D15: a followed master stopping.)

The owner's rulings of 2026-10-04:

| | Ruling |
|---|---|
| D1 | No master and another deck playing: SYNC follows that deck. A real master always wins. |
| D2 | The network loses its master: this deck claims it after a few seconds if it is playing… |
| D3 | …and not following another deck through SYNC. |
| D4 | The phase hold pauses while the jog is touched or bent, scratching, in a loop or slip; it lands again on release. |
| D5 | A track loaded while not following starts at the fader's tempo, with the fader in control. |
| D6 | The fader's target on the tempo panel shows in B and C, never in A. |
| D7 | KEY SYNC takes the master's track key only. |
| D8 | Turning keylock off while KEY SYNC is engaged keeps the shift. |
| D9 | While following, the range widens to hold a tempo beyond the fader's reach. |
| D10 | Pickup catches on a crossing, with 0.05 BPM of slack. |
| D11 | A1 with the fader in control keeps the tempo, and skips ranges that cannot express it. |
| D12 | Engaging KEY SYNC turns keylock on. |
| D13 | Mastership is handed over unasked, CDJ-style, in both directions. |
| D14 | Bars count from rekordbox's downbeat; SYNC corrects to the beat only. |
| D15 | A followed master pausing: SYNC lets go and goes dark, the tempo carries on, the fader must catch up. |
| D16 | Phase: an exact seek to land, an engine-side trim of at most 1% to hold. |
| D17 | Big tempo gaps are followed literally, never halved or doubled. |

## Status

Built. Each rule lives in one place, and the pure ones are tested without a
network:

* **Which tempo applies** — `SyncTempo::decide()` in
  `src/network/prolink/synctempo.{h,cpp}`. Every row of the eight-state table is
  a test in `src/test/synctempo_test.cpp`, named by its state rather than its
  number so a failure says which one broke.
* **What SYNC follows, and what the meter draws** — `chooseSyncSource()` and
  `chooseMeterDeck()` in `src/network/prolink/syncsource.{h,cpp}`, tested in
  `src/test/syncsource_test.cpp`.
* **Mastership** — `reconcileMastership()` and `manageMasterLikeACdj()` in
  `ProLinkSync`, on the rules in `automaster.{h,cpp}`
  (`src/test/automaster_test.cpp`). Taking and yielding on the wire are
  `Session::take_tempo_master` and its tests in `lib/prolink`
  (`crates/prolink-cxx/src/session.rs`).
* **Phase** — `ProLinkSync::followMaster()`; our clock is
  `AudibleBeatClock` (`src/test/audiblebeatclock_test.cpp`), the trim is
  `RateControl`'s `phase_trim`, and the seek is Mixxx's `beatjump` (both in
  `src/test/enginebuffertest.cpp`).
* **Pickup, range widening and starting at the fader** — the mapping,
  `mixxx_config/TriMixxx.scripts.js`, "Tempo fader".

Not writing the tempo *is* letting the fader have it, which is why the Fader
rows are a `return` rather than a branch that does something.
