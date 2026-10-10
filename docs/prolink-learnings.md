# What the CDJ-to-CDJ captures teach — the plan

Where TriMixxx, as a player on a Pro DJ Link network, behaves differently
from two real CDJ-2000NXS talking to each other, and what to change in
`lib/prolink` and the fork so it behaves as they do where it matters for
stability and compatibility: number claims, keep-alives, the master
hand-over, sync, and what it asks and answers.

Status (2026-10-10): **the plan, for review.** Nothing is built. §2 lists
the learnings, §3 the changes, §4 the checks, §5 what a real CDJ must show.

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

## 2. The learnings, ranked

| # | A real NXS does | TriMixxx does | Effect | Evidence |
|---|---|---|---|---|
| L1 | Answers another deck's number claim with `0x05` while it holds keep-alive byte `0x25` = 2, and gives that byte up when answered | Never answers; keeps its `0x25` for good | **A CDJ re-claims its number every few seconds for as long as TriMixxx is on the link** (01) | S13, S26, S10-S24, E07; B1 |
| L2 | A master that is not playing hands master to a deck that plays, unless it is synced and that deck is not | Hands it only to a synced deck | A CDJ playing out of sync is left without master behind a stopped TriMixxx | S28, E01, E05, E06; B2 |
| L3 | Sends a status packet the moment something changes; a hand-over settles in 64-133 ms | Every 200 ms, and polls every 100 ms during a hand-over | Hand-overs take 3-4 times as long; both decks claim master for up to 0.4 s | S28; B2 |
| L4 | The first deck to play takes master at once | Waits 3 s + 1 s per player number (6 s at player 4) | A CDJ that starts in those seconds takes master instead | S28, E01; B2 |
| L5 | Asks a peer about a slot once, when that slot turns loaded | Asks about both slots of every peer every 5 s | Chatter on every CDJ (02) | S13, S15a/b, S16a, S25, S26, S4b; B1, B2 |
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
  re-claims once, ~12 s later, and takes the role; it then answers the
  returning deck's claim with `0x05`.

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

TriMixxx (`automaster::successorWhenStopped`) hands over only to a deck that
is synced, so the third row differs. **B2 79.6:** the deck, master and not
in sync, stopped while NXS a played out of sync, and kept master until it
played again 15 s later. Its synced cases match: 114.616, the successor
named in the stop packet and taking master 70 ms later.

S28 has the synced cases only; the others rest on the emulator, which runs
Pioneer's own firmware.

### L3. Status on change, and the hand-over's timing

A real NXS sends status every 200 ms **and** on every change: about half of
S28's status packets come early, on ~64 ms ticks, most carrying a changed
field, and one follows each beat packet by 3-13 ms. So a hand-over is fast
(S28, five hand-overs, from the `0x26`):

| | `0x27` | holder names successor (`0x9f`) | successor claims (`0x9e`) | holder lets go |
|---|---:|---:|---:|---:|
| S28 | 2.4-5.0 ms | 3.3-5.2 ms | 16-73 ms | 67-133 ms |
| B2, NXS a asks the deck | 1 ms | **172 ms** | **233 ms** | **373 ms** |
| B2, the deck asks NXS a | 4 ms | 4 ms | **227 ms** | **284 ms** |
| B2, NXS a stops, names the deck | | in its stop packet | **351 ms** | **367 ms** |

The requester waits for `0x9f` (B2 49.877: a claims 61 ms after the deck's
`0x9f`, not after its `0x27`). TriMixxx sends status only on its 200 ms
tick and watches a hand-over by polling every 100 ms, so each step costs up
to 300 ms. It works, slowly: a hand-over takes 284-373 ms to complete
instead of 67-133 ms, and a MASTER lamp lags.

### L4. The first deck to play takes master

With nobody master, a deck takes master as it starts to play: S28 22.058
(one status packet, 65 ms, later), E01 23.269 (in the same packet). TriMixxx
claims an empty mastership only after 3 s + 1 s per player number above 1
(Sam's D2, written for a master that is *lost*): **B2: played at 4.619,
master at 10.618.** A CDJ that starts within those 6 s takes master instead,
and the deck that played first follows it.

### L5. A deck asks about a slot once

All 8 deck-to-deck media queries in the corpus (S13 19.421, S15a 20.137,
S15b 13.801, S16a 11.921, S25 12.508, S26 6.720, S4b 10.730 and 83.790) come
0.3-0.9 s after the peer's status shows that slot loaded, or 2.9 s after
first hearing a peer that has one (S26), and never again. Never one for an
empty slot. TriMixxx (`spawn_media_survey`) asks about both slots of every
peer every 5 s: B1, 28 rounds to NXS a in 160 s; B2, 46 rounds to each NXS
in 250 s, each answered. That is Sam's 02. It is not caused by 01, and L1
does not settle it.

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

Each wire change is its own commit, so it reverts alone.

| # | Change | Where | On the wire |
|---|---|---|---|
| C1 (L1) | While we say `0x25` = 2, answer another device's `0x04` claim for a number not ours with a unicast `0x05`: our number, the claim's iteration, from port 50000, as S13 and S26 send it. A `0x05` answering our own stage-3 claim ends our burst and makes us say 1. A claim for the number we hold is still met with `0x08`, as now | `prolink` `virtual_cdj.rs` | **Wire change:** a new packet, `0x05` (38 B), only ever in answer to a claim; and our `0x25` can now go from 2 to 1. Safe: it is the packet an AUTO NXS sends in the same moment (S13, S26, E07), and a MANUAL NXS takes it and settles (S13) |
| C2 (L3) | A status packet at once when what we publish changes (master claim, successor, sync, playing, play state, loaded track), at most one per 20 ms, beside the 200 ms tick. The take and the yield watch the monitor's events instead of polling every 100 ms, and act on the `0x27` the monitor already receives and drops | `prolink` `virtual_cdj.rs`, `monitor.rs`; `prolink-cxx` `session.rs` | **Wire change, timing only:** extra status packets, same content, as an NXS sends |
| C3 (L2) | A stopped master names the lowest-numbered heard deck that plays, a synced one only if we are synced; looked at every poll while we are master and stopped, not once per stop | fork `automaster.{h,cpp}`, `prolinksync.cpp` | No wire change: the same packets, in more cases |
| C4 (L4) | Our deck starting to play with no master anywhere claims at once; the delay stays for a master that is lost while we play (D2) | fork `automaster.{h,cpp}`, `prolinksync.cpp` | No wire change |
| C5 (L5) | Ask a peer about a slot when its status shows that slot turn loaded, and when a peer is first heard with a loaded slot; never on a timer, never about an empty slot | `prolink-cxx` `session.rs` | **Wire change:** fewer queries, at the moments an NXS asks |

Docs with them: `PROTOCOL.md` §2.3-2.4 (`0x25` and `0x05`), §3.1 (status on
change), §3.3 (one query per slot), §3.3b (the hand-over rules); a work-log
entry; `docs/tempo-sync.md` (C3, C4); `docs/fork-map.md` if a layer moves.

**A new version:** the prolink crates go from 0.3.0 to 0.4.0, with a
`CHANGELOG.md` saying what changed on the wire. The fork bumps `lib/prolink`
and carries C3 and C4.

## 4. The checks

- `cargo test --workspace` in `lib/prolink`, the 37-capture corpus included,
  with clippy and fmt clean. New tests:
  - C1: our `0x05` byte for byte against S13's and S26's, but for the name
    and number; every `0x04` in the corpus replayed through the rule: it
    answers only claims of another number, and only while we say 2; our
    burst ends, and we say 1, at S13's `0x05`.
  - C2: a change goes out at once and a burst of changes no faster than one
    per 20 ms; a take completes on a replayed `0x27` and `0x9f`.
  - C5: one query per slot that turns loaded; none for an empty slot.
- The fork: `automaster_test.cpp` gets E06's table (cases A-E), S28's
  hand-overs and the first play; the other ProLink tests pass.
- The emu-captures stay readable: `tools/emudump` builds against the new
  `prolink-proto` and reads E01-E17 as before.
- On emulated CDJs (two NXSs and a TriMixxx deck on one link), B1 and B2
  again with the changes:
  - A1: no re-claim after start-up, every keep-alive ~2.0 s apart, one deck
    saying 2; and with TriMixxx joining second, and restarted mid-session.
  - A2: hand-over timings within S28's; a stopped master hands over in all
    of E06's cases; the first play takes master at once; queries only on a
    slot change.

  Proposed: commit B1/B2 and A1/A2 as emu-captures E18-E21 ("a TriMixxx
  deck between two NXSs, before and after prolink 0.4"), with their NOTES.

## 5. On a real CDJ, for Sam

None of this has met a real NXS; each wire change needs Sam's check before
it merges. Both on one CDJ-2000NXS and a deck on the new build, captured
with `tools/capture-deck-to-deck.sh`:

1. **C1.** Deck on the link first, then the NXS powered on (AUTO, then
   MANUAL). The NXS claims once and settles: no `0x04` after its start-up,
   keep-alives every 2.0 s, its `0x25` = 1. Then the NXS first and the deck
   second, and both powered together: one device says 2, no re-claims.
2. **C2.** MASTER on the NXS while the deck is master, then on the deck: the
   lamps settle within ~0.15 s; `0x9f`, the new claim and the release within
   S28's times.
3. **C5.** A stick into the NXS and out again: the deck asks about it once,
   0.3-1 s after it shows loaded, and its media appear on the deck.
4. **C3 and C4** (no wire change, but DJ-visible): the deck master and
   stopped with the NXS playing out of sync: the NXS becomes master. Nobody
   master, the deck starts first: the deck is master at once.

## 6. Not in this work

- Sam's 07 (a plain stick's tempo `0xffff`) and 08 (the media response's
  8-byte sizes): unrelated to these learnings, still in his folder.
- L6 (MASTER on the master), until Sam decides.
- L7-L9, and decoding L8's bytes.
- The emulator's own differences (03-05).
