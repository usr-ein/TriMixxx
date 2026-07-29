# Status

Where the work actually is. Read this first; `docs/FINDINGS.md` has the evidence
behind every claim here.

Last updated: 2026-07-29, during a live session with two CDJ-2000NXS.

---

## The two objectives

| | Objective | State |
|---|---|---|
| **1. Consume** | See and play other CDJs' libraries | **Working end to end** in the PoC |
| **2. Serve** | Other CDJs see and play *our* library | **Working end to end** |

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

### Objective 2 — serve: a CDJ plays from us

A real CDJ-2000NXS, with nothing but a Mac and a USB stick on the other end:

| Step | State | Finding |
|---|---|---|
| Appears on the deck's LINK screen | works | F24 |
| Categories open (ARTIST/ALBUM/TRACK/GENRE/KEY/PLAYLIST) | works | F25, F26 |
| Category contents list, with pagination | works | F27 |
| Metadata + artwork on the INFO screen | works | F27 |
| NFS mount, path walk to the audio file | works | F28 |
| **Load and play a track** | works — MP3, AAC, WAV, AIFF | F30-F32, F35, F39 |
| Hot cues, preview waveform, scrubbing | works | F32 |
| LOAD SETTINGS from our medium | works | F38 |
| **Scrolling (main) waveform** | works | F33 |

S10j is the first session with **zero errors**: every request a CDJ-2000NXS
makes is answered. A load is 20 `LOOKUP`s and 201 `READ`s. Tracks play, scrub
without delay, and show hot cues and both waveforms.

---

## Phase A is done

Both objectives work against real hardware. What remains in the PoC is
tidying rather than discovery:

- the opaque prefix word (F33) must be non-zero but is still unexplained;
- argument 0 of the metadata items is an observed constant, not derived (F32);
- `GET_TRACK_INFO` item 6 is a constant `1` on all four containers; meaning
  unknown (F35);
- `0x3d03` is acknowledged with a guessed reply — no capture shows a real one.

All four supported containers play, including 75 MB lossless files read across
their whole length, and LOAD SETTINGS works. Zero dbserver errors in the
session (F39).

## Two media at once — working

```bash
sudo .venv/bin/prolinks -v serve --volume /Volumes/ONE --sd-volume /Volumes/TWO --iface en9
```

`core/medium.Medium` holds the per-slot state, `DbServer` takes `{slot: Medium}`
and resolves the medium **per message** from the descriptor's slot byte (F37 —
one connection carries both, so caching per connection would serve the wrong
library the moment the DJ switches slots). One `Vfs` holds both media under
`/C` and `/B` subtrees, which is what keeps their filehandles distinct: a handle
is a hash of the path, and a CDJ preserves only the leading 12 bytes (F28), so
two media sharing a root would be indistinguishable afterwards.

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
  FINDINGS.md      F1-F40, C1-C14, O1-O7 with evidence. Has an index.
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
tests/             248 tests, no hardware required
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
