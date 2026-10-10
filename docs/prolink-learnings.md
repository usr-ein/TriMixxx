# What the CDJ-to-CDJ captures teach — the plan

Where TriMixxx, as a player on a Pro DJ Link network, behaves differently
from two real CDJ-2000NXS talking to each other, and what to change in
`lib/prolink` and the fork so it behaves as they do where it matters for
stability and compatibility: number claims, keep-alives, the master
hand-over, sync, and what it asks and answers.

Status (2026-10-11): **built, in prolink 0.4.0 and the fork; tested without
a Mixxx build.** The plan was approved at review round 2. prolink's tests
pass (778, the corpus included), and the fork's pure rules pass natively.
Waiting on a build (Docker's VM is full until Sam frees it):
- the fork compiled whole;
- `automaster_test.cpp` under gtest;
- A1/A2 on emulated CDJs.

Waiting on Sam: the `[for Sam]` calls and the checks on a real CDJ (§5).

§2 lists the learnings, §3 the changes, §4 the checks, §5 what a real CDJ
must show.

The evidence:
- **Hardware:** `mixxx/lib/prolink/captures/`, the 37 sessions of two real
  NXSs (S-numbers below). S10-S24 also have a Mac on the link running this
  library's ancestors.
- **Emulated:** `mixxx/lib/prolink/emu-captures/` E01-E17, two NXSs on
  Pioneer's firmware.
- **TriMixxx today:** two baseline captures taken for this plan, a TriMixxx
  deck (today's main, player 4) with two emulated NXSs on one link:
  - **B1:** the deck alone on the link, then NXS a joins, then NXS b.
  - **B2:** master and sync between the three: first play, MASTER pressed
    both ways, a master that stops, SYNC both ways.

  They stay out of git for now (§4 proposes committing the before and after
  as emu-captures).

Times are seconds into the session's capture.

---

## 1. Decisions

| # | Question | Decision |
|---|---|---|
| D1 | Sam kept his ProLink problems 01 and 02 in his folder for later. L1 turns out to be 01's cause, and L5 is 02 | [nightman] L1 is in: it is a learning from CDJ-to-CDJ hardware captures, and a re-claim storm is a stability problem. Its own commit, `[for Sam]`, so he can drop it. L5 the same way, `[for Sam]` |
| D2 | L4 changes the delay of Sam's ruling D2 (`docs/tempo-sync.md`) for the first play | `[for Sam]`: built in its own commit, so it reverts alone |
| D3 | L6 changes what MASTER does on the deck that holds master | `[for Sam]`: not built. Sam decides |
| D4 | How C1 ends the conflict over `0x25` | **Step aside** (review round 1): we say 1 once another device says 2, and send nothing new. The role, and whatever number defence goes with it, stays with Pioneer's firmware. The alternative, answering claims with `0x05` as the role's holder, is named in §3 and taken only if A1 shows step-aside fails |
| D5 | C3 changes Sam's D13 hand-over in cases only the emulator shows | `[for Sam]` (review round 1): S28 has the synced cases; the rows C3 changes (neither deck synced, the master's SYNC going off) rest on E01, E06 and E09 |

## 2. The learnings, ranked

| # | A real NXS does | TriMixxx does | Effect | Evidence |
|---|---|---|---|---|
| L1 | Settles keep-alive byte `0x25` = 2 on one device: its holder answers a claim with `0x05`, and the claimer gives the byte up | Never answers; keeps its `0x25` for good | **A CDJ re-claims its number every few seconds for as long as TriMixxx is on the link** (01) | S13, S26, S10-S24, E07; B1 |
| L2 | A master that is not playing hands master to a deck that plays, unless it is synced and that deck is not | Hands it only to a synced deck | A CDJ playing out of sync is left without master behind a stopped TriMixxx | S28, E01, E05, E06; B2 |
| L3 | Sends a status packet the moment something changes; a hand-over completes in 67-133 ms | Every 200 ms, and polls every 100 ms during a hand-over | A hand-over takes 284-373 ms, and a MASTER lamp lags | S28; B2 |
| L4 | The first deck to play takes master at once | Waits 3 s + 1 s per player number (6 s at player 4) | A CDJ that starts in those seconds takes master instead | S28, E01; B2 |
| L5 | Asks a peer about a slot when that slot turns loaded, again every 5 s until answered, then never | Asks about both slots of every peer every 5 s, answered or not | Chatter on every CDJ (02) | S13, S15a/b, S16a, S25, S26, S4b, S10, S10b; B1, B2 |
| L6 | MASTER pressed on the master hands master to the other deck that plays | MASTER on the master does nothing | What the DJ's MASTER does | E09 only |

Documented, not changed: L7-L10, and what TriMixxx already does as an NXS
does, at the end of this section.

### L1. The `0x25` role is negotiated, and its holder answers claims

**Keep-alive byte `0x25` is not latched at boot,** as prolink's docs have it
(F9). One device on a link says 2 and every other says 1, and they settle it
with a packet prolink never sends:

- **S13 17.389-17.392:** player 1 (MANUAL) joins; player 2 (AUTO, saying 2)
  answers 1's first stage-3 claim (`0x04`) with a unicast `0x05` carrying its
  own number, 2 ms later. Player 1 sends no more claims and says 1.
- **S26 3.602-5.606:** both players booted together (both AUTO) and both say
  2. At its next keep-alive slot (5.605) player 2 re-claims its number;
  player 1 answers with `0x05` 1 ms later; player 2 says 1 from then on.
- **S02, S2c:** with MANUAL decks, nobody sends `0x05`; the joiner hears the
  other's keep-alive and says 1.
- **E07 23.611, 56.165:** when the deck saying 2 leaves, the survivor
  re-claims once, ~12 s later, unanswered, and takes the role; it then
  answers the returning deck's claim with `0x05`.

**A deck that keeps saying 2 beside another that says 2 re-claims for ever.**
The `0x05` that would settle it never comes from TriMixxx:

- **All 20 serve sessions, S10-S24** (the Mac on the link): the Mac and the
  NXS both say 2, and the NXS sends 18-249 claims a session (0.5-0.9 a
  second), its keep-alives a median 2.0-2.6 s apart instead of 2.0 s.
  S13 and S26 have no re-claim after the settling one.
- **B1, today's main:** NXS a joins the deck. Nobody answers its claim, so it
  claims three times and says 2 beside the deck's 2. It then re-claims every
  4.5-5.2 s for the rest of the capture: 96 claims in 160 s, its keep-alives
  4.5-5.2 s apart. NXS b joins at 64.4; a answers b's second claim with
  `0x05` (67.348) and b settles at once.
- **Sam's 01** is this. "Only the last CDJ to join re-claims" is whichever
  CDJ ends up saying 2 next to TriMixxx: a CDJ that is answered says 1 and
  stops (01's at-1x capture, 40.210: CDJ 1 stops the moment CDJ 2 answers
  it; CDJ 2 then re-claims 212 times).

**What no capture shows,** and why C1 leaves the role to an NXS:

- **Who defends a number.** Every `0x05` and every `0x03` in the corpus and
  the emu-captures came from the device saying 2, and each `0x03` sender
  also held the number it defended (B1 66.149, E10-E17, 01's at-1x runs).
  Whether a deck saying 1 still defends its own number against an AUTO
  joiner's proposal is not shown. A TriMixxx holding the role would have to
  do whatever the role does, and could confirm a duplicate number it never
  saw contested.
- **MANUAL, not AUTO.** prolink claims as MANUAL (`virtual_cdj.rs`, its
  `0x02` claim's `0x31`). Every `0x05` in the corpus came from an AUTO deck,
  and MANUAL incumbents send none (S02, S2c). A deck that steps aside and
  sends no `0x05` behaves as the MANUAL deck it announces.

### L2. A stopped master hands over to a deck that plays

What a master that is not playing does when another deck plays:

| master | deck that plays | hands over | evidence |
|---|---|---|---|
| in sync | in sync | yes | S28 193.655 and 208.276; E05 44.524, 69.511; E06 D |
| not in sync | in sync | yes | E05 11.532; E06 A |
| not in sync | not in sync | yes | E01 78.031; E06 C; E09 10.040 |
| in sync | not in sync | no | E06 B |
| (the last, then its SYNC off) | not in sync | yes, at once | E06 E |

It names the successor at `0x9f` in the very packet that says it stopped,
nothing on 50001, and the rule is held: a deck that starts later, or the
master's own SYNC going off, hands over too.

**"Plays" is the status flag `0x40`,** which goes with beats and master. An
NXS playing a plain file sits at play state 3 with `0x40` clear, sends no
beats and never takes master (E12; `emu-captures/PLAIN-STICKS.md`). Play
state alone would offer master to a deck that never takes it.

TriMixxx (`automaster::successorWhenStopped`) hands over only to a deck that
is synced, so the third row differs. **B2 79.6:** the deck, master and not
in sync, stopped while NXS a played out of sync, and kept master until it
played again 15 s later. Its synced cases match: 114.616, the successor
named in the stop packet and taking master 70 ms later.

S28 has the synced cases only; the others rest on the emulator, which runs
Pioneer's own firmware.

### L3. Status on change, and the hand-over's timing

A real NXS sends status every 200 ms **and** on every change: about half of
S28's status packets come early, most carrying a changed field, and one
follows each beat packet by 3-13 ms. None of S28's 4,834 status packets
came less than 61.7 ms after the one before to the same peer; the early
ones cluster at ~64 ms. So a hand-over is fast (S28, five hand-overs, from
the `0x26`):

| | `0x27` | holder names successor (`0x9f`) | successor claims (`0x9e`) | holder lets go |
|---|---:|---:|---:|---:|
| S28 | 2.4-5.0 ms | 3.3-5.2 ms | 16-73 ms | 67-133 ms |
| B2, NXS a asks the deck | 1 ms | **172 ms** | **233 ms** | **373 ms** |
| B2, the deck asks NXS a | 4 ms | 4 ms | **227 ms** | **284 ms** |
| B2, NXS a stops, names the deck | | in its stop packet | **351 ms** | **367 ms** |

The requester takes master on `0x9f`, not on the `0x27`: B2 49.877, a claims
61 ms after the deck's `0x9f`; and in all five S28 hand-overs the holder's
`0x27` and the status carrying its `0x9f` left within 1 ms of each other.
TriMixxx sends status only on its 200 ms tick and watches a hand-over by
polling every 100 ms, so each step costs up to 300 ms. It works, slowly: a
hand-over takes 284-373 ms to complete instead of 67-133 ms, and a MASTER
lamp lags.

### L4. The first deck to play takes master

With nobody master, a deck takes master as it starts to play: S28 22.058
(one status packet, 65 ms, later), E01 23.269 (in the same packet). TriMixxx
claims an empty mastership only after 3 s + 1 s per player number above 1
(Sam's D2, written for a master that is *lost*): **B2: played at 4.619,
master at 10.618.** A CDJ that starts within those 6 s takes master instead,
and the deck that played first follows it.

"Nobody is master" is only known once every player has been heard: a peer
unicasts status only after it has heard our keep-alive (PROTOCOL §3.1), and
a claim older than a second is not believed. S28's deck at 22.058 had been
hearing its peer for 22 s.

### L5. A deck asks about a slot until it is answered

All 8 answered deck-to-deck media queries in the corpus (S13 19.421, S15a
20.137, S15b 13.801, S16a 11.921, S25 12.508, S26 6.720, S4b 10.730 and
83.790) come 0.3-0.9 s after the peer's status shows that slot loaded, or
2.9 s after first hearing a peer that has one (S26), and are not repeated.
Never one for an empty slot. **Unanswered, an NXS asks again every 5.03 s:**
S10, 22 times while the Mac never answered; S10b, at 4.29, 9.32, 14.35 and
20.44 s, answered at the fourth and never asked again.

TriMixxx (`spawn_media_survey`) asks about both slots of every peer every
5 s, answered or not: B1, 28 rounds to NXS a in 160 s; B2, 46 rounds to each
NXS in 250 s, each answered. That is Sam's 02. It is not caused by 01, and
L1 does not settle it.

### L6. MASTER on the master

E09 4.913: MASTER pressed on the deck that holds master hands master to the
other playing deck, in its status alone. TriMixxx's MASTER does nothing
there (`ProLinkSync`'s comment says a CDJ does nothing either). S28 never
tried it.

### Documented, not changed

- **L7. Pitch copies while paused.** A paused NXS zeroes status `0x98` and
  `0xc4` (S28: 274 of 275 paused packets; S06: all 323); TriMixxx writes the
  pitch to all four copies. No known reader.
- **L8. Unknown bytes at idle values.** The status template is an idle
  deck's packet, and TriMixxx leaves bytes there that move on a playing NXS:
  `0x113` (S28: 0x05 playing, 0x15/0x17 paused, 0x07 cued; TriMixxx always
  0x15), `0x87`, and `0x116`-`0x11b`. Play state after a load is 0x06 (cued)
  on an NXS and 0x05 on TriMixxx. No known reader; decoding them is a
  follow-up.
- **L9. Defending a number.** An AUTO NXS proposing a taken number in its
  `0x02` claim is answered by the holder with `0x03` [01 01 01], and moves on
  (emulated: B1 66.149). TriMixxx would answer with `0x08`, which no NXS sends
  in any capture. TriMixxx takes the highest free number and an AUTO NXS
  proposes from 1 up, so the two meet only on a full link. A follow-up.
- **L10. Already as an NXS (B2).** Keep-alive layout byte for byte; media
  query and answer layouts; status unicast per peer every 200 ms; beats on
  time (p05-p95 -4.0 to +1.8 ms between beats against `60/(bpm x pitch)`;
  S28's sd 1.3-1.9 ms); the successor named in the stop packet; a deck
  handed master keeps SYNC lit (E05; B2 155.016); a deck that plays again
  does not take master back.

## 3. The changes

| # | Change | Where | On the wire |
|---|---|---|---|
| C1 (L1) `[for Sam]` | **Step aside.** Another device's keep-alive saying `0x25` = 2 makes us say 1 from our next keep-alive on, for good; so does a `0x05` answering our own stage-3 claim, which also ends our burst there (S13 17.392, S26 5.606). We never send `0x05`. A claim for our own number is met with `0x08`, as now | `prolink` `virtual_cdj.rs` | **Wire change, one byte:** our keep-alive's `0x25` goes from 2 to 1 when an NXS says 2, as S26's player 2 did; our burst can end early. No new packet type |
| C2 (L3) | A status packet at once when what we publish changes (master claim, successor, sync, playing, play state, loaded track), never sooner than 62 ms after the last one (S28's floor, 61.7 ms), beside the 200 ms tick. The take acts on the holder's `0x9f` naming us, and the yield on the successor's claim, as monitor events instead of a 100 ms poll | `prolink` `virtual_cdj.rs`, `monitor.rs`; `prolink-cxx` `session.rs` | **Wire change, timing only:** extra status packets, same content, at no more than an NXS's rate |
| C3 (L2) `[for Sam]` | A stopped master names the lowest-numbered heard player whose status flag `0x40` is set, a synced one only if we are synced. It offers again only when something changes (a new candidate, our SYNC going off), never twice to one deck in one stop | fork `automaster.{h,cpp}`, `prolinksync.cpp`; the bridge's `Player` gains `is_playing`, the `0x40` flag | No wire change: the same packets, in more cases |
| C4 (L4) `[for Sam]` | Our deck starting to play claims an empty mastership at once when every player in the device table has sent status in the last second; otherwise D2's delay, as now | fork `automaster.{h,cpp}`, `prolinksync.cpp` | No wire change |
| C5 (L5) `[for Sam]` | Ask a peer about a slot when its status shows that slot turn loaded, or when a peer is first heard with it loaded; again every 5 s while unanswered; never once answered, until the slot empties and loads again; never about an empty slot | `prolink-cxx` `session.rs` | **Wire change:** fewer queries, at the moments an NXS asks, with its retry |

**C1's alternative,** if A1 (§4) shows an NXS still re-claiming beside our 1:
while we say 2, answer another device's `0x04` claim **for a player number**
(1-6) not ours with a unicast `0x05` (our number, the claim's iteration,
from port 50000), as S13 and S26 send it. No mixer is in any capture, so a
mixer's claim is never answered. Its pass mark in A1 is stricter: every
device ends on its own number, and NXS b's proposal of NXS a's number is
still met with a `0x03` from a while a says 1. Switching costs one commit in
`virtual_cdj.rs`, and no new plan round.

**Commits that revert alone.** prolink's `CHANGELOG.md` opens with an
Unreleased section first. Each wire change (C1, C2, C5) then lands in its
own commit with its own `PROTOCOL.md` and `CHANGELOG.md` lines, so dropping
any one is a single revert. Last, a release commit names the section 0.4.0
and bumps the crates. In order:

1. prolink: `CHANGELOG.md`; C1; C2; C5; the bridge's `is_playing`; the
   work-log entry; 0.4.0.
2. The fork: C3; C4; the `lib/prolink` bump.
3. TriMixxx: `docs/tempo-sync.md` (C3, C4), this doc's status, the `mixxx`
   bump.

`PROTOCOL.md` gains: §2.3-2.4, `0x25` and `0x05` (with C1); §3.1, status on
change (with C2); §3.3, the query and its retry (with C5); §3.3b, the
hand-over rules (with C2). `docs/fork-map.md` changes only if a layer moves.

**A new version:** the prolink crates go from 0.3.0 to 0.4.0, with a
`CHANGELOG.md` saying what changed on the wire. The fork bumps `lib/prolink`
and carries C3 and C4.

## 4. The checks

Docker's VM has 7.9 GB free and a Mixxx build needs 8, so no Mixxx build
runs until Sam frees it. The fork links prolink into Mixxx, so an emulated
deck can only run the changes after a build.

**Tonight, with no build** (`lib/prolink` on the Mac):
- `cargo test --workspace` with the 37-capture corpus, clippy and fmt clean.
  New tests:
  - C1: a keep-alive saying 2 from another device makes ours say 1, ours
    staying 1 after; S13's and S26's `0x05`, replayed at our claim, end our
    burst and make us say 1; every keep-alive and `0x05` in the corpus
    replayed through the rule: we step aside only for another device's 2,
    or a `0x05` addressed to us.
  - C2: a change goes out at once, and never sooner than 62 ms after the
    last status; a take completes on a replayed `0x9f` naming us; a yield
    completes on the successor's replayed claim.
  - C5: one query per slot that turns loaded; a query answered only on its
    third try is asked three times, 5 s apart, then never again; none for an
    empty slot; a swap (loaded, empty, loaded) asks again.
  - The bridge: `is_playing` follows the `0x40` flag, not the play state.
- `tools/emudump` builds against the new `prolink-proto` and reads E01-E17
  as before.

**After a Mixxx build:**
- The fork: C3 and C4, and `automaster_test.cpp` with E06's table (cases
  A-E), S28's hand-overs, a plain-file deck (state 3, `0x40` clear) never
  offered master, one offer per candidate per stop, and C4 claiming at once
  only with every player heard. The other ProLink tests pass.
- On emulated CDJs (two NXSs and a TriMixxx deck on one link), B1 and B2
  again with the changes:
  - **A1 (C1):** TriMixxx first; NXS a joins, unanswered, and says 2; our
    `0x25` goes to 1 within one keep-alive; a re-claims at most once after
    that and never again; its keep-alives 2.0 s apart. NXS b joins and is
    answered by a. **Every device ends on its own number** (pass mark),
    exactly one device (an NXS) says 2. Then TriMixxx joining second, and
    its session restarted mid-capture. If a keeps re-claiming beside our 1,
    C1 switches to its alternative and A1 runs again with that one's pass
    mark.
  - **A2:** hand-over timings within S28's; E06's cases A-E with the deck as
    the stopped master; the first play takes master at once; queries only
    when a slot turns loaded.

  Proposed: commit B1/B2 and A1/A2 as emu-captures E18-E21 ("a TriMixxx
  deck between two NXSs, before and after prolink 0.4"), with their NOTES.

## 5. On a real CDJ, for Sam

None of this has met a real NXS; each wire change needs Sam's check before
it merges. On real CDJ-2000NXSs and a deck on the new build, captured with
`tools/capture-deck-to-deck.sh`:

1. **C1.** The deck on the link first, then **two NXSs in AUTO**, one after
   the other. **Each ends on its own number** (the pass mark); one NXS says
   `0x25` = 2 and the deck 1; no `0x04` after each NXS's start-up;
   keep-alives every 2.0 s. Then the same with the NXSs in MANUAL; then the
   NXSs first and the deck second; then all powered together.
2. **C2.** MASTER on an NXS while the deck is master, then on the deck: the
   lamps settle within ~0.15 s; `0x9f`, the new claim and the release within
   S28's times.
3. **C5.** A stick into an NXS and out again: the deck asks about it once,
   0.3-1 s after it shows loaded, and its media appear on the deck.
4. **C3 and C4** (no wire change, but DJ-visible): the deck master and
   stopped with an NXS playing out of sync: the NXS becomes master. Nobody
   master, the deck starts first: the deck is master at once.

## 6. Not in this work

- Sam's 07 (a plain stick's tempo `0xffff`) and 08 (the media response's
  8-byte sizes): unrelated to these learnings, still in his folder.
- L6 (MASTER on the master), until Sam decides.
- L7-L9, and decoding L8's bytes.
- The emulator's own differences (03-05).
