# The preview waveform — how to build it

A small waveform of the **selected** track, drawn in the info panel under the
cover art, for both kinds of medium: a stick in this deck and a slot on a CDJ.

This is a plan, not a report. Nothing in it is built yet.

It sits under [`browser-prd.md`](browser-prd.md), which put the waveform out of
scope for that round (§2) and left the info panel as artwork plus a field list
(§8.2). Everything else it relies on — the info panel, `deck_library`, the
ANLZ readers, the dbserver client, the track cache — is already there.

---

## 1. Where the picture comes from

rekordbox writes four waveforms per track, in two files beside the audio:

| Tag | File | Size | What it is |
|---|---|---|---|
| `PWAV` | `.DAT` | **400 bytes** | The preview: one packed byte per column. |
| `PWV2` | `.DAT` | 100 bytes | The tiny one, on a CDJ's track list. |
| `PWV3` | `.EXT` | 150/s, 1 B | The scrolling detail waveform, blue/white. |
| `PWV4` | `.EXT` | 1200 × 6 B | The colour preview (NXS2). |
| `PWV5` | `.EXT` | 150/s, 2 B | The colour detail waveform. |

`PWV5` is the one the deck already reads — `rekordboxwaveform.cpp` turns it into
Mixxx's own waveform and summary for the track under the needle. That is a
different job from this one and stays where it is.

**Use `PWAV`.** Four reasons, in the order they matter:

1. **It is the only one both sources can give us cheaply.** A CDJ answers
   `GET_WAVEFORM_PREVIEW` over dbserver with `PWAV` (plus `PWV2`) and nothing
   else; the colour previews are behind a request nothing in `lib/prolink`
   implements yet (§5.4). Picking `PWAV` means one decoder, one look, and local
   and remote media that are actually identical — which is what §11.4 of the PRD
   asks for and what `PWV4` would quietly break.
2. **400 bytes.** A whole 700-track stick is 280 kB of waveform. Nothing about
   the budget needs thinking about again.
3. **400 columns is exactly the width we have.** See §3 — one byte, one pixel,
   no resampling anywhere in this feature.
4. It is in the `.DAT`, which every export has for every track. The `.EXT` is
   NXS2-era and not guaranteed.

### 1.1 One packed byte

```
  bit  7 6 5   4 3 2 1 0
       └─────┘ └───────┘
       whiteness  height
        0..7      0..31
```

Height is the bar. "Whiteness" is what a CDJ shades with — high-frequency
content, roughly — and is why a CDJ's preview is blue with white in the busy
parts. Both are used (§3.2).

This layout is not a guess: `prolink-proto`'s `analysis::waveform_preview()`
already splits exactly these two fields to serve a player, verified against a
real deck-to-deck load, and its unit test spells out `0b101_00110` →
whiteness 5, height 6.

## 2. The value type

One type, produced by two decoders, consumed by one renderer:

```cpp
// src/library/deck/previewwaveform.h
namespace mixxx::deck {

/// A track's preview waveform: 400 columns, one packed byte each, exactly as
/// rekordbox's `PWAV` tag stores them.
///
/// A value type on purpose. It is built on a worker thread and read on the GUI
/// thread, and 400 bytes is cheaper to copy than to share.
class PreviewWaveform final {
  public:
    static constexpr int kColumns = 400;

    /// From a `PWAV` payload, as a local `.DAT` stores it. Empty unless the
    /// payload is exactly kColumns bytes.
    static PreviewWaveform fromPwav(const QByteArray& payload);
    /// From a `GET_WAVEFORM_PREVIEW` reply, as a player puts it on the wire:
    /// 800 bytes of (height, whiteness) pairs, then 100 bytes of `PWV2`.
    static PreviewWaveform fromWire(const QByteArray& blob);

    bool isNull() const { return m_columns.isEmpty(); }
    quint8 height(int column) const;    ///< 0..31
    quint8 whiteness(int column) const; ///< 0..7

  private:
    QByteArray m_columns; ///< kColumns bytes, or empty.
};

} // namespace mixxx::deck
```

Both decoders are pure functions of a byte array, which is what makes them
testable from a literal — the same reason `prolink-proto/src/analysis.rs` takes
bytes rather than a parsed file.

**Refuse rather than approximate.** A payload that is not the length it should
be gets an empty `PreviewWaveform` and one warning, and the panel draws no
strip. `rekordboxwaveform.cpp` already sets this precedent for
`len_entry_bytes() != 2`, and the reason holds here: a plausible but wrong
waveform is worse than none, because nothing on screen says it is wrong.

## 3. Drawing it

### 3.1 The geometry, and what it costs the field list

The info panel is 464 × 460 (`kInfoPanelWidth`, and 36 + 48 + **460** + 56 = 600
down the screen). Today it spends that on 20 px of padding, a 180 px cover, and
eight field rows of 30 px — `paintEvent` silently stops drawing when it runs out,
so eight is already the cap and a fully-populated track already loses its
Comment.

The strip has to come out of that. The budget that keeps all eight fields:

| | px |
|---|---|
| Padding | 16 |
| Cover | 160 |
| Gap | 12 |
| **Preview strip** | **44** |
| Gap | 12 |
| 8 field rows × 27 | 216 |
| **Total** | **460** |

So: cover 180 → 160, row height 30 → 27, and the panel loses nothing it draws
today. Both are one-line changes to the constants at the top of
`wdeckinfopanel.cpp`. The 17 px value font still clears a 27 px row.

The strip is **400 px wide, centred** — `(464 − 400) / 2 = 32` px either side.
One column, one pixel, no scaling, no interpolation, no resampling. This is the
whole reason `PWAV` is the right tag.

### 3.2 The painting

```
column i  →  x = 32 + i
height h  →  bar of (h * 44 + 15) / 31 pixels, drawn UP from the baseline
whiteness w → lerp(kWaveBlue, white, w / 7.0)
```

Bottom-up from a baseline, the way a CDJ draws a preview, not centre-mirrored
the way Mixxx draws an overview. This deck's idiom is the CDJ's everywhere else
and a DJ reads this shape faster for it.

`kWaveBlue` is new to the palette in `wdeckinfopanel.cpp`: `0x33, 0x88, 0xdd`,
spelled out beside the others for the same reason they are — a delegate and a
custom-painted panel paint raw, and half a stylesheet is worse than none.

### 3.3 Rendered once, not per repaint

The panel repaints on **every encoder detent**. The cover already learned this:
it is decoded in `setTrack()` and never in `paintEvent()`, because decoding a
JPEG per frame is visible on a Pi.

So `setPreview()` renders the 400 lines into a `QPixmap` once and `paintEvent()`
blits it. 400 `drawLine` calls is sub-millisecond even on this hardware, but
paying it 60 times a second to draw an identical picture is exactly the kind of
cost this panel has already been bitten by.

Only the **current** pixmap is kept — 400 × 44 ARGB is 70 kB, and there is only
ever one track selected.

### 3.4 The three states

| State | Drawn as |
|---|---|
| Have it | The waveform. |
| Asked, not arrived | A 1 px baseline across the strip. |
| No preview exists, or it failed | A 1 px baseline across the strip. |

**The strip is always reserved**, even for a track that will never have one. The
panel follows the selection with no click, so a strip that collapsed would move
every field 56 px up and down as the encoder turned — unreadable, and a far
worse cost than 44 px of background. The baseline says "a waveform belongs here
and there is not one", which is the honest thing for both of the empty cases;
distinguishing *pending* from *absent* is not worth a spinner for something that
lands in single-digit milliseconds off a stick.

### 3.5 Not in the track rows

The default layout's 72 px rows keep cover, title, artist, BPM and key, and get
no waveform. Seven visible rows means seven previews in flight per scroll
position, which on a remote medium is seven dbserver round trips per flick — the
exact shape of the artwork prefetch the PRD already refused (§11.1). The info
panel shows one track, and one is what this feature fetches.

## 4. Local media

The path is in hand already: `deck_library.analyze_path` is the track's `.DAT`,
written by `PdbIngest` for every row of every medium, and it is what
`loadSelectedTrack()` already reads to import grid, cues and waveform.

### 4.1 Parse it in Rust, not with the vendored Kaitai types

There are two ANLZ readers in this tree and **the Rust one is the better
parser**:

| | `lib/rekordbox-metadata` (Kaitai, C++) | `prolink-rekordbox::anlz` (binrw, Rust) |
|---|---|---|
| A tag that does not match its schema | Throws; the file is lost | `content` is `None`, bytes kept — costs that tag, not the file |
| A tag it has never heard of | Not in the enum | `FourCc` is a newtype; `PWV6`/`PWV7` survive |
| `PWAV` | `wave_preview_tag_t::data()`, raw | `PreviewColumn::height()` / `whiteness()`, named |
| Two `PCOB` tags | Caller's problem | `cue_lists()`, because the first is usually the wrong one |
| Raw payload for the serve side | Not offered | `Tag::payload()`, byte for byte |

The last two rows are the tell: that reader was written by someone who had hit
those cases. And its `PreviewColumn` doc names the evidence for the exact bit
layout this feature depends on — *"confirmed by the wire transform: serving a
preview splits each byte into `(height = b & 0x1f, whiteness = b >> 5)` (F30)"*
— which comes from a captured deck-to-deck load, not from a schema.

**It is also already the deck's ANLZ reader on one side.** `serve/medium.rs`
holds an `AnlzFile` per served track and `serve/dbserver/analysis.rs` pulls
`FourCc::PWAV` out of it to answer a CDJ asking us for exactly this waveform.
Reading `PWAV` off a local stick is not new code — it is code the deck runs
already, reached from C++ instead of from the serve loop.

So:

```
analyze_path  →  prolink::readAnlz()  →  AnlzFile::parse
              →  payload(FourCc::PWAV)  →  PreviewWaveform::fromPwav()
```

**This already exists.** `read_anlz` was added to `crates/prolink-cxx` when the
Kaitai reader was retired (§13), with `AnlzContents::preview` carrying the
`PWAV` payload as its 400 packed bytes, and `network/prolink/prolinkanlz.h`
wraps it in Qt types the way `prolinkpdb.h` wraps the database. So the preview
feature has no parser work left in it at all — it reads
`contents.preview` and decodes one byte per column.

`read_anlz` is a **free function**, not a `Session` method, which is what makes
it safe to call from the worker thread in §6.2: `Session` owns a tokio runtime
and its methods are not; `read_pdb` and `read_anlz` are.

One thing to watch, and it is the reason §12 keeps a fast path in reserve:
`read_anlz` decodes the whole file — grid, cues, both waveforms — to reach 400
bytes. That is a few milliseconds for one selected track and does not matter;
for a whole-medium sweep it might, and the fix is a `PWAV`-only entry point that
stops at the tag it wants.

### 4.2 The caveat the crate states about itself

`anlz.rs`'s header says no captured `.DAT` was available when it was written, so
its layouts came from the crate-digger schema and its tests are synthetic. That
is worth knowing and, for this feature, nearly empty: `PWAV` is 400 undecorated
bytes whose packing is corroborated by captured *wire* bytes (above). The one
field the crate flags as genuinely unsettled is which nibble of a `PWV2` byte
holds the height, and this feature never reads `PWV2`.

An empty `columns`, or a length that is not 400, both mean "no preview here" —
not an error and not a warning. A stick always has a few.

### 4.3 …and the reader it replaced

Choosing Rust here would have left the C++ Kaitai reader still running the load
path, so the browser would have parsed ANLZ one way when you select a track and
another when you load one. That is not a state to stay in, and §13 is what was
done about it instead: **the Kaitai parser is gone from the build entirely.**

## 5. A CDJ's media

### 5.1 dbserver, not NFS

`GET_WAVEFORM_PREVIEW` (`0x2004`) asks a player for exactly this blob by
rekordbox track id and gets **900 bytes** back: `PWAV` unpacked to 800 bytes of
(height, whiteness) pairs, then `PWV2` appended verbatim. `DbClient::analysis()`
in `lib/prolink` already implements the request, including the two things that
make it awkward — the track id is at argument **2** rather than 1, and the
declared fifth argument is absent from the wire.

The alternative — fetching the `.DAT` over NFS with the existing `fetch_file` —
is wrong three times over, and each of the three is already written down in this
codebase:

- **NFS runs one transfer at a time**, and a streaming audio fetch holds that
  turn for the length of a whole track. `MediaRegistry::startStreaming()` fetches
  the grid *before* the audio for precisely this reason. A preview queued behind
  a download would arrive minutes after the row it belongs to.
- **Asking NFS for small files by the hundred churns the player's filehandle
  table until it answers `NFSERR_STALE` to everything** — including the track a
  DJ is loading (F49). That is why artwork moved to dbserver.
- 20–40 kB per track over the wire, for 400 bytes of it.

900 bytes on a connection that is already open, with no filesystem involved at
either end. Nothing is written to disk on this path at all.

### 5.2 Sharing the artwork connection

`Session::fetch_artwork` holds one `DbClient` behind a `tokio::sync::Mutex` and
serialises every cover through it. Previews go through **the same queue**.

A separate connection would need a second browsable device number, which is
contended with the decks (F45) and is the one resource this feature must not
spend. The cost of sharing is that a preview can queue behind the covers a
scrolling list asked for — a screenful is seven rows, deduplicated once per path,
so the wait is a few round trips, order 100–300 ms. That is worth measuring
(§10) and not worth pre-empting: a priority queue in front of a mutex is real
complexity for a delay nobody has yet seen.

### 5.3 The bridge change

`fetch_artwork` is the shape to copy exactly — it takes a device number, spawns
onto the tokio runtime, and reports through the event queue, so **the calling
thread never blocks**. What it does not do is give bytes back, because artwork
goes to a file.

Two new bridge functions, in `crates/prolink-cxx/src/lib.rs`:

```rust
/// Ask a player for a track's preview waveform. Returns a transfer id;
/// the bytes arrive as a `TransferDone` and are collected with
/// `take_waveform_preview`.
fn fetch_waveform_preview(self: &Session, device: u8, slot: Slot, track_id: u32)
        -> Result<u32>;

/// Take the bytes of a finished preview fetch, by transfer id. Empty if
/// the fetch failed or has already been taken.
fn take_waveform_preview(self: &Session, transfer: u32) -> Vec<u8>;
```

The spawned task locks the artwork queue, calls
`client.analysis(slot, track_id, Analysis::WaveformPreview)`, stashes the bytes
in an `Arc<Mutex<HashMap<u32, Vec<u8>>>>` beside the session, and posts the same
`TransferDone` event `finish()` already builds. Drop the client on error, as
`fetch_artwork` does — a failed request leaves the connection mid-message and the
protocol has no resynchronisation point (F16).

Two alternatives, both worse: adding a `Vec<u8>` payload to the shared `Event`
struct puts an unused field on every beat packet; writing the 900 bytes to a
temp file and reading it back is what `isDatabase` does in
`prolinknetworkservice.cpp` and would need no new bridge function at all — worth
remembering if `take_waveform_preview` turns out awkward, but a filesystem round
trip for 900 bytes we are holding is silly.

On the C++ side, `ProLinkNetworkService` gains a `isPreview` flag on `Pending`,
takes the bytes in the `TransferDone` arm of `poll()`, and emits

```cpp
void previewFetched(const QByteArray& mac, MediaSlot slot,
        quint32 trackId, const QByteArray& blob, const QString& error);
```

`MediaRegistry` forwards it, resolving the (mac, slot) back to a `MediumId` the
way it already does in both directions.

### 5.4 Colour, later or never

Locally, colour is nearly free now: `prolink-rekordbox` decodes `PWV4` into a
`ColorPreviewColumn` with its six bytes named, so it is one more field on
`AnlzPreview` (§4.1).

Remotely it is not. `PWV4` reaches a client over `ANLZ_TAG_REQ` (`0x2202`),
which `lib/prolink` does not implement and which nothing in the capture corpus
exercises. So colour today means **local media looking different from remote
ones**, which is the divergence §11.4 of the PRD exists to prevent, for a
prettier picture.

Monochrome on both, then, until a capture settles `0x2202`. And when it does,
`PWV4`'s six bytes want checking against the files rather than trusting the
decode: the layout in common circulation for its sibling `PWV5` is wrong, which
is written up at length in `rekordboxwaveform.cpp` and was caught only by
correlating against `PWV3`. The Rust crate names those six fields as *reported*,
not as measured.

## 6. Keeping the GUI thread free

### 6.1 The rules

The deck has been burned by all four of these:

1. **No file read and no socket read on the GUI thread.** Not 400 bytes, not
   from a stick that is spinning up.
2. **No `block_on`, and no nested `QEventLoop`.** `Session::metadata()` and
   `root_menu()` block the calling thread inside the tokio runtime;
   `fetchCompanionBlocking()` spins a nested loop with `ExcludeUserInputEvents`.
   Both are freezes with a timeout on them. Neither is allowed here — the
   `fetch_artwork` spawn-and-report shape is the only one this feature uses.
3. **Workers return values; the GUI thread mutates state.** The worker builds a
   `PreviewWaveform` and hands it over with
   `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. It never touches the
   cache, and it never touches a widget. `TrackCache::prefetch()` is the
   pattern.
4. **A worker outliving what asked for it is normal.** Selection moves faster
   than I/O. Late results are cached and dropped, never applied blind — see the
   generation counter below.

### 6.2 The reader

A new `PreviewWaveformCache`, owned by `WDeckBrowser` beside `TrackCache`:

```cpp
class PreviewWaveformCache : public QObject {
    Q_OBJECT
  public:
    /// What is already in RAM. Never reads a file, never asks the network:
    /// safe from a paint.
    PreviewWaveform lookup(const MediumId& medium, quint32 rbId) const;

    /// Ask for one. Returns immediately whatever happens.
    void request(const MediumId& medium, quint32 rbId, const QString& analyzePath);

    /// Everything queued that is not this track. Called when the selection
    /// moves, so a spin does not leave a queue of rows nobody is looking at.
    void keepOnly(const MediumId& medium, quint32 rbId);

  signals:
    void arrived(QString mediumKey, quint32 rbId);
};
```

- **Its own `QThreadPool`, `setMaxThreadCount(1)`.** Not the global pool: that
  one carries Mixxx's analysers and `TrackCache::prefetch()`, and four threads
  reading a USB stick at once is slower than one, not faster. One thread also
  makes "cancel everything queued" meaningful.
- **A generation counter.** Every request carries the value the counter had when
  it was made; the worker checks it before parsing and returns early if the
  selection has moved on twice over. A queued read of a file that nobody wants
  costs a filesystem round trip on a stick that may be busy copying a track.
- **Remote requests are one in flight, ever.** The next one is sent when the
  previous lands. A CDJ answers dbserver off the same processor that decodes its
  audio and has been seen to take seconds mid-track.

### 6.3 When a preview is asked for

| Situation | Local | Remote |
|---|---|---|
| Selection moves, info panel open | Immediately | After a **150 ms** dwell |
| Selection moves, panel closed | Nothing | Nothing |
| Rows either side of the selection | ±3, low priority | Nothing |
| The whole medium, in the background | Optional sweep, §6.4 | **No** |

The 150 ms remote dwell is the same idea as the 300 ms prefetch dwell and half
the length, because the cost being avoided is a round trip rather than a whole
track. Locally there is nothing to defer: the read is a few milliseconds on a
thread nobody is waiting on.

The ±3 look-ahead is local-only for the same reason the audio prefetch is
(`wdeckbrowser.cpp`, the dwell timer): a remote medium has no file to read and
speculating costs the network.

**Nothing is fetched while the info panel is closed.** The default layout has
nowhere to draw a waveform, and a DJ who never opens the panel should pay
nothing for this feature at all.

### 6.4 "Every track", honestly

The brief asks for every track's preview to be pulled and calculated. The panel
shows one track at a time, so most of that work is never seen — and the two
sources are not alike enough to answer the same way:

**Local: affordable, and worth it.** 700 tracks × ~30 kB of `.DAT` is 21 MB off
the stick and a few seconds of one core, for 280 kB of result. Do it as a sweep
that starts when a medium turns Ready, runs on the same single worker at the
back of the queue, sleeps ~5 ms between files, and **stops the moment a
foreground request arrives or a track copy starts** — USB bandwidth is the
constraint that the track cache is already fighting for, and a waveform nobody
is looking at must never delay a track somebody is loading. The payoff is that a
DJ spinning the encoder at speed never sees an empty strip.

**Remote: no.** 650 tracks is 650 dbserver round trips, on the one connection
that also feeds cover art, queued ahead of whatever the DJ does next. That is
the artwork prefetch the PRD deleted in §11.1, rebuilt. Remote previews are
fetched for the row being looked at and nothing else.

If the local sweep turns out to cost more than it looks (§10), it is one flag to
turn off, and everything still works — the on-demand path is the same code.

## 7. What it costs

| | |
|---|---|
| One preview, decoded | 400 B, plus ~32 B of `QByteArray` |
| A 700-track stick, swept | ~300 kB |
| LRU cap, 4096 entries | **1.8 MB** |
| The rendered strip | 70 kB, one of them |
| Local read, per track | ~30 kB off the stick, a few ms of one core |
| Remote fetch, per track | **900 B** on the wire, no disk at either end |
| Written to the SD card | **Nothing** |

Against 3288 MB available and a 421 MB Mixxx. This is heap, not tmpfs: it does
not touch tier 1's cap and cannot push the deck towards zram (§12.2 of the PRD).

Entries are dropped when their medium vanishes — the rows are gone from the
browser, so the previews are 300 kB held for nothing. This is the opposite of
`markUnreachable()` on the track cache, and the difference is the point: a
cached *track* is irreplaceable once the stick is out, a cached *waveform* is a
picture of a track that can no longer be played.

## 8. Failure, and the edges

| Case | Behaviour |
|---|---|
| No `PWAV` in the `.DAT` | Baseline. Cached as empty, so it is never asked for twice. |
| `.DAT` missing or unparseable | Same. One `qWarning`, not one per repaint. |
| Payload the wrong length | Refuse and warn once (§2). Never approximate. |
| A CDJ that answers something other than 900 bytes | Accept ≥ 800 and take the first 800 as pairs; refuse anything shorter. A CDJ-3000 may have more to say and the first 800 bytes are still `PWAV`. |
| Player leaves mid-fetch | `TransferDone` with `ok = false`. Cached as empty; asked again if the medium comes back, because the empty is keyed to a medium that went away. |
| Stick pulled | Previews already read stay in RAM until the medium's entries are dropped; the rows go first, so nobody sees them. |
| Reply arrives for a row nobody is on | Cached, not drawn. It cost nothing extra and it is probably three detents behind. |

## 9. What to build, in order

Each step is worth having on its own, and each is testable before the next.

1. **`PreviewWaveform` and its two decoders**, with unit tests from byte
   literals — `src/library/deck/previewwaveform.{h,cpp}`,
   `src/test/previewwaveform_test.cpp`. No I/O, no Qt widgets, no hardware.
2. **The panel.** Re-budget the geometry (§3.1), add `setPreview()`, render once
   (§3.3), draw the baseline states. Feed it from a hard-coded array first: the
   look is settled on screen, before any of the plumbing exists.
3. **The local reader** — Kaitai, one worker thread, generation counter — and
   wire it to the selection. This is the whole feature working, for the source
   that is actually plugged in.
4. **The cache and the scheduler**: LRU, negative caching, `keepOnly()`, the
   ±3 look-ahead, drop-on-vanish.
5. **The Rust bridge** (§5.3), then the `ProLinkNetworkService` and
   `MediaRegistry` legs, then the 150 ms remote dwell. Needs a CDJ to verify,
   like everything else on that side.
6. **The local sweep** (§6.4), behind a flag, last — it is the only part that
   can slow something else down.
7. **Diagnostics** (§10).

## 10. Diagnostics

Into the "Media and cache" section of the diagnostics page (PRD §14.5), because
this is where the two guesses in this document get settled:

- Previews cached, bytes held, hit rate.
- Local: reads done, mean and worst parse time, sweep progress or `off`.
- Remote: fetches done, failures, **mean and worst round trip**, and how long a
  preview waited behind cover art. That last number is the one that says whether
  §5.2's shared queue was the right call.
- Bytes written to the SD card by this feature, which must read `none`.

## 11. Deliberately out

- **Colour previews** (§5.4).
- **Cue and loop markers on the strip.** They are in the same files, and the
  strip is 44 px tall — a memory cue and a hot cue three seconds apart are the
  same pixel.
- **Waveforms in track rows** (§3.5).
- **Reusing this for the deck's own waveform.** That comes from `PWV5` through
  `rekordboxwaveform.cpp`, at 150 points per second and in colour, and it is a
  better picture from a different tag.
- **Persistence.** Nothing survives a reboot, as with everything else in the
  browser (PRD §15). A stick re-read costs a few seconds of one thread.

## 12. Left to measure

1. **What the local sweep actually costs** with a track copying at the same
   time. If a load gets measurably slower with the sweep running, the throttle
   is wrong or `AnlzFile::parse` is too heavy for a whole medium — in that
   order. The second fix is a `PWAV`-only fast path in `read_anlz_preview`,
   which stops at the tag it wants instead of decoding the grid and the cues to
   throw them away.
2. **How long a remote preview waits behind cover art** (§5.2). If a flick can
   put a preview a second behind the selection, the artwork queue needs to be a
   two-priority channel rather than a mutex.

## 13. Retiring the Kaitai parser — done

Two readers for one file format is how two readers drift — the argument §11.4 of
the PRD made about `export.pdb`, applied to the files beside it. The pdb half
was already done; this was the ANLZ half.

**Nothing outside `src/library/rekordbox/` included a Kaitai type.** Three files
did, and `lib/kaitai` was linked into exactly one target — `rekordbox_metadata`
— which was linked into exactly one place. So it was a bounded piece of work
with a real end: **9 420 lines deleted**, four parts.

| | What | Lines out | Risk |
|---|---|---|---|
| **A** | Delete `RekordboxFeature` | 1 655 | Low — dead already |
| **B** | Port the grid and cues to Rust | ~280 rewritten | **Medium — the load path** |
| **C** | Port the `PWV5` waveform to Rust | ~50 rewritten | Low |
| **D** | Delete `lib/rekordbox-metadata` and `lib/kaitai` | 6 732 | None, once A–C land |

**Not verified on hardware.** The Rust side has tests and they pass; the C++
side has not been compiled, let alone run against a stick — see §13.5 for what
that means and what to try first.

### A. `RekordboxFeature` is dead code

The old sidebar's rekordbox node: 1 516 lines of feature plus its header, still
constructed at `library.cpp:103` behind `ShowRekordboxLibrary`, still watching
for sticks, still parsing every `export.pdb` it finds with the Kaitai
`rekordbox_pdb` into a `rekordbox_library` table **that nothing reads any more**.
`RekordboxFeature` and `RekordboxPlaylistModel` are referenced from that one
`addFeature()` call and nowhere else in the tree.

The PRD settled its fate in §2 — *"the old library view goes away completely. No
compatibility path, no preference to bring it back"* — and it has already cost
real bugs by outliving that: its twin ran a second Pro DJ Link session and the
deck announced "no player number was free" against itself (F2 in
`browser-status.md`).

Deleting it takes the entire Kaitai **pdb** parser out of the build on its own —
3 257 of the 6 732 lines in part D — and is three edits: the two files, the
include and `addFeature()` in `library.cpp`, and two lines of `CMakeLists.txt`.

### B. The grid and the cues

`readAnalyzeFile()` reads exactly three tags — `PQTZ`, `PCOB`, `PCO2` — and the
Rust crate decodes all three, with the two-`PCOB` trap (memory cues in one, hot
cues in the other) handled by `cue_lists()` rather than left to the caller.

The seam is clean, and worth keeping clean: **the parse moves, the apply does
not.** Frame arithmetic, the mp3 timing offset, the eight rekordbox cue colours,
Mixxx's `CuePointer` construction and the "grids only from the legacy `.DAT`"
rule all stay exactly where they are in C++. What changes is where the numbers
come from — one `read_anlz(path)` returning beats and normalised cues, in place
of a Kaitai section walk.

This is the part that wants a hardware round rather than a code review: it is
the path with beat grids and hot cues verified on a real stick behind it.

### C. The `PWV5` waveform

`readColourWaveform()` becomes a bridge call and the rest of
`rekordboxwaveform.cpp` — the band contrast, the upsample/downsample rules, the
interleaved-stereo fix — is untouched, because none of it is parsing.

**The two agree on the bit layout, which is the thing that could have blocked
this.** `rekordboxwaveform.cpp` derived bits 15–13 / 12–10 / 9–7 / 6–2 by
correlating against `PWV3` after the circulated layout came out at 0.13; the
Rust `ColorDetailColumn` names those same four fields, and its module header
says the layout *was* measured against the interpretation in common circulation.
Two independent arrivals at the same answer.

### D. Delete the parsers

`lib/rekordbox-metadata` (5 530 lines of generated C++ plus the two `.ksy`
schemas) and `lib/kaitai` (1 202), the `rekordbox_metadata` and `Kaitai` CMake
targets, and the `-Wno-switch` workaround the generated code needs. The `.ksy`
files are upstream and `prolinks-compat/ksy/` keeps a copy, so nothing
unrecoverable goes.

### What this is not

Not a rewrite of the analysis logic, and not a change to what the deck draws. A
correct port changes nothing on screen: same grid, same cues, same waveform,
from the same bytes, through a parser that keeps a malformed tag from costing
the whole file.

### 13.5 Three things that did change, and one that nearly did

The port was meant to be behaviour-preserving. Three places would not survive
being ported literally, and each is worth knowing about before the next load
looks different:

1. **A track with no `.EXT` had no beat grid.** `readAnalyzeFile`'s flag was
   named `ignoreCues` and meant *grid only*, so the call that handled a missing
   `.EXT` — `ignoreCues = false` — read the cues and skipped the `PQTZ` tag
   sitting right there in the `.DAT`. Any medium written by a rekordbox old
   enough not to write `.EXT` files loaded ungridded. `applyAnalysis` now
   applies the grid from the `.DAT` unconditionally and takes cues from the
   `.EXT` when there is one, which is what the comment above it always claimed.

2. **The main cue dereferenced an empty optional.** A plain `PCOB` memory cue
   carries no colour, and the first one chronologically became the main cue via
   `pMainCue->setColor(*memoryCueOrLoop.color)` — undefined behaviour, not a
   default colour, reachable on exactly the media in (1). Now guarded.

3. **The `.EXT` was parsed twice**, once for cues and once for the waveform,
   because both were reached by a path. Each file is now read once in
   `applyAnalysis` and the parsed contents passed down. This is why
   `importWaveforms` takes an `AnlzContents` rather than a path.

And the one that nearly changed: **`prolink-rekordbox` had the `PWV5` colour
mapping documented wrong.** Its bit *positions* and band assignment match what
`rekordboxwaveform.cpp` measured — the two arrived at bits 15-13 / 12-10 / 9-7 /
6-2 independently — but three doc lines named the render colours in field order,
so `treble` claimed green and `mid` claimed blue. Mixxx renders `filtered.mid`
green and `filtered.high` blue, so porting by the comment rather than by the bit
field would have swapped two thirds of every waveform. Ported by bit field, and
the comments are corrected upstream.

### 13.6 What has and has not been checked

Built for arm64 and running on the deck. The build found exactly one fault — a
third reference to the preferences checkbox, in `setDefaults()`, where neither
of the two obvious call sites is.

| | |
|---|---|
| `prolink-cxx` unit tests for `read_anlz` | **6, passing** — beats, both cue lists, a point's leftover loop time, the colour bit layout, the packed preview |
| The whole `lib/prolink` workspace | **passing**, clippy clean |
| arm64 build, `--target export` | **clean** |
| Media read on the deck | **SAM2, 692 tracks, 35 playlists** |
| Beat grid | **imported** — `beats true`, grid lines drawn, no analyser pass |
| Waveform | **imported** — 51 472 and 49 975 `PWV5` columns on two tracks, peak band 254/255 |
| Hot cues | **imported, with their rekordbox colours** — 1 and 5 drawn on the waveform of a track carrying eight |
| Cue comments | **decoded** — the labels read as text, which is the UTF-16BE path that replaced `fromUtf16BeString` |
| Main cue | **placed** |
| A track with **no `.EXT`** | **not tested** — every track on this stick has one |
| The Pro DJ Link paths | **not tested** — needs a CDJ |

### 13.7 The waveform that looked wrong and was not

The first track loaded after the port drew almost entirely red, against a
reference screenshot from before it full of red, yellow and green. That is
exactly what a swapped band mapping would look like, and it is the failure this
whole area has produced once already.

It was the track. Three independent decodes of the same `.EXT` off the deck —
Python straight from the bytes, the Rust reader, and the picture on screen —
agree: **bass 6.60, mid 2.51, treble 1.52** out of 7. With `kBandContrast` at
2.2 that puts green at `(2.51/6.60)^2.2` = 12 % of red, which is a deep red
waveform and always was.

Across nine tracks on that one stick the mid-to-bass ratio runs from **0.23 to
1.52** — 4 % green to saturated — so "mostly red" and "yellow and green" are
both ordinary outputs of the same unchanged code, decided by the music. Loading
a track at the other end of that range drew red, green, cyan and magenta.

Worth writing down because the wrong conclusion was one step away and would have
sent someone rewriting a decode that was correct. **Three decodes of the same
bytes settled it in a few minutes; reasoning about the screenshot would not
have settled it at all.**
