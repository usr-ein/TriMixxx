# Streaming a track off a CDJ

How the deck plays a track that lives on another player's stick, starting
before the file has arrived.

## The problem

A remote track used to be fetched whole and then played. A 60 MB lossless file
over Pro DJ Link's NFS runs about 1.5 MB/s, so that is **forty seconds of
staring at a progress ring** before a sound comes out. Nobody mixes like that.

The obvious fix — point a decoder at the file while it downloads — has a
failure mode that is much worse than the wait:

> The local file is created at its **full size** from the first instant, so it
> is sparse. Every byte not yet written **reads back as a zero, successfully**.
> There is no error, no short read, no signal of any kind. A decoder let loose
> on it plays silence and every layer above reports that everything is fine.

Silence mid-set is worse than a pause and far worse than an error. So the whole
design is about one question: *which bytes are really there?*

## The shape of it

```
 Rust (lib/prolink)          C++ (src/library/deck)         FFmpeg
 ──────────────────          ──────────────────────         ──────
 fetch_file_streaming
   open + stat  ──────────►  onFetchProgress(total)
                             build StreamingFile
                             register it ─────────────────► SoundSourceProLink
   read head    ──────────►  markPresent(0, 1M)              claims the path
   read tail    ──────────►  markPresent(size-256k, 256k)    AVIOContext
   read middle  ──────────►  markPresent(...)                read → blocks
   ...                                                       seek → answers
   TransferDone ──────────►  complete()                          AVSEEK_SIZE
```

Five pieces, each with one job:

| Piece | Where | Job |
|---|---|---|
| `progressive_plan` | `crates/prolink/src/consume/nfs.rs` | Which ranges, in what order |
| `fetch_streaming` | `crates/prolink-cxx/src/session.rs` | Fetch them, announce each one |
| `PresentRanges` | `src/library/deck/streamingfile.h` | Which bytes have arrived |
| `StreamingFile` | same | A read of an absent range **blocks** |
| `SoundSourceProLink` | `src/sources/` | An `AVIOContext` served by the above |

## Order: head, tail, middle

The head is the runway. The tail is second and that is not an aesthetic
choice: **M4A and MP4 keep the `moov` atom at the end**, and a decoder cannot
open one without it. If the tail waited its turn in the middle, AAC would not
open until the whole track had arrived — and *only* AAC would be broken, which
is a horrible fault to track down. `tail_comes_second_or_aac_never_opens` in
`crates/prolink/tests/progressive.rs` is there to keep it that way.

The middle then follows the playhead, in chunks, so the download stays ahead of
where the decoder is reading.

**Head size is 1 MB**, about 25 seconds of a 320 kbps MP3. Deliberately not
larger: the tail is fetched *after* it, so every byte of head is delay before
the first sound of an AAC track. The link delivers roughly thirty seconds of
audio per second of wall clock, so the download is never what runs out — the
head exists for the case where the network stalls.

## The one trap, and where it is nailed down

`TransferProgress` carries `done`, a running total. It is **not** a contiguous
prefix, because the head and the tail are counted together and they are not
next to each other. The obvious `markPresent(0, done)` therefore claims the gap
between them — which is a hole, and plays as silence.

So progress events carry `offset` and `len`, the exact range that landed, and
that is the only thing `markPresent` is ever given.

`the_running_total_is_not_a_contiguous_prefix` pins this, and the field's own
doc comment in `crates/prolink-cxx/src/lib.rs` says it in the place someone
would read before reaching for `done`.

Two related rules, both learned the same way:

- **Every step is announced, including the head.** An earlier version
  suppressed the first event so that "first event" could mean *head and tail
  are both down* — and silently lost the head's range with it. A range that
  lands without being announced is one a reader blocks on until it times out:
  a hang, not a stutter. `every_step_is_announced` covers it.
- **Flush before announcing.** The reader is a different file descriptor onto
  the same file. Announcing a range still sitting in the writer's buffer invites
  a read of bytes that are not there.

## What blocks, and for how long

`WDeckBrowser::loadSelectedTrack()` runs on the GUI thread, and two things on
this path make it wait.

**The size.** `MediaRegistry::startStreaming()` spins a nested event loop until
the first progress event arrives. That event carries nothing but `total`, and
is emitted the moment the file has been opened and stat'd — so the wait is one
connect, mount, lookup and stat. Well under a second, and it is the minimum
possible: a reader that blocks on absent ranges needs *only* the length.

**The ANLZ files.** Fetched in full, before the audio, with the same bounded
loop. Tens of kilobytes each.

Fetching them *first* is not tidiness. The library runs one transfer at a time
against a player, and a streaming fetch holds its turn for the entire download.
Anything queued behind it would wait for the whole track — so a beat grid asked
for afterwards would arrive minutes late, long after the `Track` it had to be
applied to existed.

Everything else waits on the **reader's** thread, inside `StreamingFile::read`,
which is where waiting is free.

One known wart, accepted rather than engineered around: `applyAnalysis` calls
`mp3guessenc` on the audio file to work out rekordbox's decoder timing offset,
and it reads the file with plain stdio rather than through the SoundSource — so
on a track that is still arriving it scans zeros where the holes are and may
classify wrongly. The offset it picks is between 0 and 50 frames, so the worst
case is a beat grid out by about **one millisecond**, and only until the track
is loaded again after the download finished.

Both loops run with `ExcludeUserInputEvents`, and `loadSelectedTrack` snapshots
every field it needs off the model *before* either of them. During a nested loop
the model can be re-sorted or reset and a `QModelIndex` does not survive it.

## Where the bytes live

Both roots are on **tmpfs**, so a night of remote loads writes nothing to the
SD card:

- Audio: `/run/trimixxx/cache`, via `TrackCache::localPathFor()` — hashed on
  (medium, path), so two players holding clones of one stick do not collide.
  `adopt()` puts it on the cache's books, and the browser pins it immediately,
  with no event loop in between: an eviction sweep in that gap could drop the
  very file about to be played.
- ANLZ and artwork: `/run/trimixxx/remote/<medium key>`, which is what
  `MediaRegistry::remoteCacheRoot()` returns. This used to be under
  `CacheLocation` — on the card, and never cleaned up, so every medium ever
  seen left its covers there for good.

## "Is it already here?" cannot be answered by the filesystem

A half-streamed file is on disk at its full size with zeros in the gaps. It is
indistinguishable from a complete one by looking at it, and it *plays* — as
silence. So completion is tracked explicitly, in `m_streamComplete`, and a file
that is not recorded there is deleted and re-streamed rather than trusted.

The one place this must not fire is a medium that has gone: the local copy may
by then be the only one in existence. `startStreaming` checks the medium is
still in the registry **before** it deletes anything.

## Unloading a track that is still arriving

The stream is **not** abandoned and **not** unregistered. The download is left
running — those bytes are already paid for, and a complete file in tier 1 is
what makes playing it again instant — and, more importantly, the record of
which ranges have landed has to outlive the load. A reload before the download
finishes could otherwise not tell a real byte from a sparse hole, and would have
to wait for the whole file.

The cost is that a read blocked at the moment of the unload stays blocked until
the next range lands: under a second in practice, bounded by the read timeout
regardless, and on the reader's own thread. `m_removeWhenDone` sweeps the entry
up when the transfer finishes.

## Observability

The things worth watching, and where they surface:

| Signal | Where | What it means |
|---|---|---|
| `playable after N ms` | log, per load | The size wait. Should be well under a second. |
| `waiting for N bytes at X` | log, debug | A read lost the race with the download. |
| `waited N ms ... total W waits / T ms` | log, info | Cumulative. A burst precedes a stutter. |
| `download complete: ...` | log, info | The switch to ordinary reads is safe. |
| **Streaming** section | diagnostics page | Size, complete/arriving, and the wait counters, live. |

A healthy remote track waits a handful of times while it opens and then never
again. A growing wait count is the warning that comes before the audio breaks
up, and it is invisible everywhere else.

## What is *not* streamed

- **`export.pdb`** — fetched whole. A truncated one parses far enough to look
  plausible and yields a library missing its last few hundred tracks.
- **ANLZ `.DAT`/`.EXT`** — fetched whole, up front, as above.
- **Artwork** — over dbserver, not NFS, and fire-and-forget. A cover that
  arrives late is used the next time the row is drawn.
- **Anything on a local stick.** `SoundSourceProviderProLink::newSoundSource`
  returns null for a path that is not in the `StreamingFileRegistry`, and
  `SoundSourceProxy` falls straight through to the ordinary decoders.

## Testing it

Without hardware — 20 in `progressive.rs`, 25 in gtest, all green on arm64:

```
cd mixxx/lib/prolink && cargo test --locked
docker buildx build --platform linux/arm64 --target unittest \
  --build-arg GTEST_FILTER='StreamingFile*:PresentRanges*:StreamingArrival*' .
```

`StreamingArrivalTest` is the end-to-end one: a real sparse file, a background
writer delivering ranges in the real order, and a reader going front to back
asserting that no byte it gets is a hole. Everything below the socket is real.

With hardware, the list is in `browser-status.md` §1.
