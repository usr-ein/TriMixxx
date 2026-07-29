# Status

Where the work actually is. Read this first; `docs/FINDINGS.md` has the evidence
behind every claim here.

Last updated: 2026-07-29, during a live session with two CDJ-2000NXS.

---

## The two objectives

| | Objective | State |
|---|---|---|
| **1. Consume** | See and play other CDJs' libraries | **Working end to end** in the PoC |
| **2. Serve** | Other CDJs see and play *our* library | **Playing.** Main waveform still missing |

### Objective 1 — consume: working

Against a real CDJ-2000NXS, **passively** (we transmit nothing on any DJ-Link
port — enforced by a guard that raises before the `sendto` syscall):

```
rpcinfo   -> portmapper 111, mountd 48276, nfsd 2049          F10
exports   -> '/C/'  groups=['169.254.0.0/255.255.0.0']        F12
pull-db   -> 1,077,248 B, 842 READs, 0 retries, 1459 KiB/s    F13
tracks    -> 692 tracks, 329 artists, 275 albums, 35 lists    F14
```

The anchor test passes: the NFS-fetched `export.pdb` is byte-identical to the
same file read off the physically ejected stick, modulo two header bytes the
deck itself rewrites (F13 — this is why the Mixxx cache key must be
`stable_digest`, not a raw hash).

### Objective 2 — serve: a CDJ browses us, but will not load

A real CDJ-2000NXS, with nothing but a Mac and a USB stick on the other end:

| Step | State | Finding |
|---|---|---|
| Appears on the deck's LINK screen | works | F24 |
| Categories open (ARTIST/ALBUM/TRACK/GENRE/KEY/PLAYLIST) | works | F25, F26 |
| Category contents list, with pagination | works | F27 |
| Metadata + artwork on the INFO screen | works | F27 |
| NFS mount, path walk to the audio file | works | F28 |
| **Load and play a track** | works | F30, F31, F32 |
| Hot cues, preview waveform, scrubbing | works | F32 |
| **Main (detail) waveform** | **does not display** | O7 |

S10i records 1141 NFS READs; two tracks loaded and scrubbed with no delay.
The one remaining defect is the main waveform.

---

## What is being worked on right now

1. **O7 — the main waveform does not display.** Everything else in a load
   works. The only remaining difference from a real deck's replies is the
   fifth prefix word of `BEAT_GRID` and `WAVEFORM_DETAIL`, which we send as
   zero and cannot derive — it is a free-running counter on the serving deck.
   Those are exactly the two replies feeding the main waveform. See F32.

## Not started

- **Phase B — Mixxx C++ integration** (`research/10`, Phase B). Including **B0**,
  the two pre-existing Rekordbox bugs the user approved fixing upstream:
  the `buildPlaylistTree()` cross-thread `appendChild()`, and
  `location TEXT UNIQUE` colliding for two devices holding cloned media.

---

## Layout

```
STATUS.md          this file — current state
README.md          what the repo is, how to run it
docs/
  FINDINGS.md      F1-F29, C1-C14, O1-O6 with evidence. Has an index.
  HARDWARE.md      runbook for a session with real CDJs
  CAPTURE-PLAN.md  the S1..S10 capture scenarios
research/
  00..09           protocol specification (phase-1 deliverable)
  10               the approved build plan for Mixxx
  ref-repos/       cloned upstream projects (git-ignored, reference only)
prolinks_poc/
  proto/           pure codecs, no I/O — both directions for every format
  net/             sockets, event loop, RPC/NFS client *and* server
  core/            discovery, announcer, library model, slots
  capture/         journal recorder, pcap reader, passivity guard
tests/             183 tests, no hardware required
tools/capture.sh   start a named scenario capture
captures/
  S*/              named scenarios; NOTES.md and cmd.txt are tracked, pcaps are not
  journals/        the recorder's own JSONL from unnamed runs; disposable
```

## Licensing, in one line

The PoC is **GPLv2-or-later** to match Mixxx. Protocol *facts* are not
copyrightable and every reference repo may be read; only **prolink-connect** and
**prolink-cpp** (both MIT) may have *code* adapted. python-prodj-link
(Apache-2.0), dysentery (EPL-1.0), vizlink (EPL-2.0) and libcdj (no licence) are
reference-only — see C5.
