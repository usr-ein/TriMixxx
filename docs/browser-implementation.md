# The Browser — implementation plan

Companion to [`browser-prd.md`](browser-prd.md), which is signed off. This is
*how*, in the order it gets built. Section references like §8.2 point at the PRD.

Everything here lands in the Mixxx fork (`mixxx/`, branch `main`) except where
marked `mixxx_config/` or `pi_config/`.

---

## 1. The two ideas that make this small

Most of this design's apparent size collapses onto two mechanisms that already
exist in the tree.

### 1.1 Every menu is a temporary view

`BaseExternalPlaylistModel::setPlaylistById()` does something more useful than
its name suggests: it builds a `CREATE TEMPORARY VIEW` yielding
`(track_id, position)` and points `BaseSqlTableModel::setTable()` at it. The
model then does sorting, searching, cover art and column mapping for free.

Nothing about that is playlist-specific. A genre, an album, a BPM bucket, a
search result set and a play history are all "a set of track ids in an order" —
so **every track list in the browser is one view and one model class**, and the
whole query catalogue (§6) is a list of `SELECT`s. No new model machinery.

It also gives "Default" (§9.2) for free: the view carries the intended order in
`position`, the model's default sort is `position ascending`, and
`[Library],sort_reset` already exists to drop back to it.

### 1.2 One parser, one ingest, one table

Today the same file format is read twice: local sticks via the Kaitai
`rekordbox_pdb`, remote media via the Rust `lib/prolink`. Two parsers, two
writers (`rekordboxfeature.cpp`'s SQL half and `prolinkdbwriter.cpp`), two
tables with different columns.

The Rust bridge already exposes both entry points needed:

```
fn read_pdb(path: &str) -> PdbContents        // a stick, straight off the mount
fn read_pdb_bytes(bytes: &[u8]) -> PdbContents // a fetched remote database
```

So local and remote converge on one `PdbContents`, one writer, one schema. Two
parsers become one, ~1000 lines of ingest become ~350, and every category works
identically on both sources because there is only one thing to query.

**Scope correction to the PRD:** this unifies the **PDB** half only. The
**ANLZ** half — beat grids, cues and waveforms, in `rekordboxanalysis.cpp` and
`rekordboxwaveform.cpp` — also uses Kaitai (`rekordbox_anlz.h`), and both
sources already share that code, so it is not a divergence and not a blocker.
`prolink-cxx` exposes no ANLZ surface today; adding one is real work with no
user-visible benefit, so it stays Kaitai. `lib/rekordbox-metadata/rekordbox_pdb.*`
goes; `rekordbox_anlz.h` and `lib/kaitai/` stay. Deleting Kaitai outright is a
follow-up, not part of this.

## 2. Shape

```
                        ┌──────────────────────────────────────┐
   hardware ─ MIDI ───▶ │  [Browser] controls                  │
   touch panel ───────▶ │  WDeckBrowser        (skin node)     │
                        │   ├── breadcrumb bar                 │
                        │   ├── QStackedWidget of pages        │
                        │   │     DeckListView + delegates     │
                        │   ├── WDeckSortMenu    (overlay)     │
                        │   ├── WDeckKeyboard    (overlay)     │
                        │   └── WDeckInfoPanel                 │
                        └───────────────┬──────────────────────┘
                                        │
              ┌─────────────────────────┼──────────────────────────┐
              ▼                         ▼                          ▼
      ┌───────────────┐        ┌────────────────┐        ┌──────────────────┐
      │ MediaRegistry │        │ DeckTrackModel │        │   TrackCache     │
      │  live media   │        │ BaseSqlTable-  │        │ tier1 tmpfs      │
      │  + state      │        │ Model over a   │        │ tier2 SD         │
      └───────┬───────┘        │ temp view      │        │ pin / LRU /      │
              │                └────────┬───────┘        │ prefetch         │
              ▼                         ▼                └────────┬─────────┘
      ┌───────────────┐        ┌────────────────┐                 │
      │ MediumReader  │───────▶│   PdbIngest    │                 ▼
      │ local: read_  │        │  deck_library  │        ┌──────────────────┐
      │ pdb(path)     │        │  deck_playlists│        │ PlayerManager    │
      │ remote: fetch │        │  deck_playlist_│        │ loadTrackToPlayer│
      │ + read_bytes  │        │  tracks        │        └──────────────────┘
      └───────┬───────┘        └────────────────┘
              │
      ┌───────┴────────┐
      ▼                ▼
  /media/DJ_USB_*   ProLinkNetworkService
  + volume label    (already exists)
```

`WDeckToast` and `WDeckDiagnostics` are separate skin nodes; the toast subscribes
to `MediaRegistry`, the diagnostics page to everything.

## 3. What gets deleted

| Path | Why |
|---|---|
| `src/library/rekordbox/rekordboxfeature.{cpp,h}` | Replaced by `MediumReader` + `PdbIngest`. Its ANLZ siblings stay. |
| `src/library/prolink/prolinkdbwriter.{cpp,h}` | Folded into `PdbIngest`. |
| `src/library/prolink/dlgprolinkfetch.{cpp,h}` | No modal fetch dialog any more (§11.3). |
| `lib/rekordbox-metadata/rekordbox_pdb.{cpp,h,ksy}` | Second PDB parser. |
| Most `addFeature()` calls in `src/library/library.cpp` | Auto DJ, playlists, crates, browse, recording, history, iTunes, Traktor, Serato, Rhythmbox, Banshee (§1). |
| `<LibrarySidebar>`, `<Library>`, `<SearchBox>`, `<PowerColumn>` in `skin.xml` | Replaced by `<DeckBrowser>`; POWER moves to a root-menu row (§5). |
| `TriMixxx.sortAdvance / sortApply / sortClear / sortForget / ledSort` cycle | Replaced by the sort menu (§9.3). |

Kept but no longer shown: `MixxxLibraryFeature`. `Library` holds a pointer to it
and the collection is still where `Track` objects are registered, so it stays
constructed and unreferenced by the UI. Ripping it out is a separate cleanup.

**Do not delete the ProLink feature's status HTML** — `statusHtml()` and
`serveHtml()` move wholesale into the diagnostics page (§14).

## 4. New code

### `src/library/deck/` — data

| File | Responsibility |
|---|---|
| `mediumid.h` | `MediumId{Source, key}`. `usb:/media/DJ_USB_1`, `prolink:<mac>\|<slot>`. Value type, hashable. |
| `mediaregistry.{cpp,h}` | The live list of media and their state. Signals for appear / change / vanish / read-failed. The single source of truth for level 0 and for toasts. |
| `mediumreader.{cpp,h}` | Worker: local → `read_pdb(path)`; remote → `fetch_database()` then `read_pdb_bytes()`. One at a time, off the GUI thread, reports progress. |
| `pdbingest.{cpp,h}` | `PdbContents` + `MediumId` → SQL. The only writer. |
| `volumelabel.{cpp,h}` | Mount point → label (§8.1). |
| `decktrackmodel.{cpp,h}` | The one track model. `setQuery(name, sql)` → temp view → `setTable()`. |
| `deckcategorymodel.{cpp,h}` | Value lists: genres, artists, albums, labels, keys, BPM buckets, dates. Counts and cover paths. |
| `bpmbuckets.{cpp,h}` | Anchored tiling + the density peak (§7.4). Pure, unit-testable. |
| `harmonickeys.{cpp,h}` | Camelot adjacency (§8.3). Pure, unit-testable. |
| `playlog.{cpp,h}` | This boot's plays; merges with stick history for Last played (§7.6). |
| `trackcache.{cpp,h}` | Tiered cache, pinning, LRU, prefetch (§12). |
| `deckbrowsercontrol.{cpp,h}` | The `[Browser]` control group. |

### `src/widget/deck/` — UI

| File | Responsibility |
|---|---|
| `wdeckbrowser.{cpp,h}` | Skin node. Navigation stack, breadcrumb, page switching, overlay management, control wiring. |
| `decklistview.{cpp,h}` | `QListView` subclass: encoder movement, selection band, `QScroller` kinetic scroll, tap / long-press / swipe recognition (§4.2). Every list uses it. |
| `deckdelegates.{cpp,h}` | `SourceRow`, `MenuRow`, `ValueRow` (with optional cover), `PlaylistRow` (2×2 stitch), `TrackRow`, `TrackInfoRow`. |
| `wdeckinfopanel.{cpp,h}` | The right-hand panel (§8.2). |
| `wdecksortmenu.{cpp,h}` | The pop-over and its two-step flow (§9.1). |
| `wdeckkeyboard.{cpp,h}` | The alphabetical keyboard (§10). |
| `wdecktoast.{cpp,h}` | Skin node. The notification stack (§13). |
| `wdeckdiagnostics.{cpp,h}` | Skin node. HTML page + sparklines + `SystemProbe`. |
| `systemprobe.{cpp,h}` | `/proc/stat`, `/proc/meminfo`, `/proc/swaps`, thermal, `vcgencmd get_throttled`, `df`, interfaces. Ring buffers for the sparklines. |

Registration follows the pattern the fork already uses for `WProLinkPhaseMeter`
and `WTempoPanel`: a `parseX()` in `LegacySkinParser` plus a branch in
`parseNode()`.

## 5. The schema

One medium column, one unique key, and the columns the PRD's categories need
that neither existing table has (`date_added`, `label`, `play_count`).

```sql
CREATE TABLE deck_library (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    medium            TEXT NOT NULL,      -- MediumId::key()
    rb_id             INTEGER NOT NULL,   -- rekordbox track id
    title TEXT, artist TEXT, album TEXT, genre TEXT, label TEXT, comment TEXT,
    year INTEGER, tracknumber TEXT,
    duration INTEGER, bitrate TEXT, bpm FLOAT, rating INTEGER,
    key TEXT, key_id INTEGER,             -- key_id drives Camelot sort AND notation
    color INTEGER,
    date_added TEXT,                      -- as stored, YYYY-MM-DD
    play_count INTEGER,
    location TEXT,                        -- path relative to the medium root
    analyze_path TEXT,
    artwork_path TEXT, artwork_id INTEGER,
    coverart TEXT, coverart_source INTEGER, coverart_type INTEGER,
    coverart_location TEXT, coverart_color INTEGER, coverart_digest BLOB,
    UNIQUE(medium, rb_id)
);
CREATE INDEX deck_library_medium        ON deck_library(medium);
CREATE INDEX deck_library_medium_genre  ON deck_library(medium, genre);
CREATE INDEX deck_library_medium_artist ON deck_library(medium, artist, album);
CREATE INDEX deck_library_medium_bpm    ON deck_library(medium, bpm);

CREATE TABLE deck_playlists (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    medium TEXT NOT NULL, rb_id INTEGER NOT NULL, parent_rb_id INTEGER NOT NULL,
    name TEXT, is_folder INTEGER, sort_order INTEGER,
    UNIQUE(medium, rb_id)
);
CREATE TABLE deck_playlist_tracks (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    playlist_id INTEGER REFERENCES deck_playlists(id),
    track_id    INTEGER REFERENCES deck_library(id),
    position    INTEGER
);
CREATE TABLE deck_history (             -- the stick's own history playlists
    medium TEXT, track_id INTEGER, session INTEGER, position INTEGER
);
CREATE TABLE deck_play_log (            -- ours, this boot only
    track_id INTEGER, played_at INTEGER
);
```

`UNIQUE(medium, rb_id)` is the fix both existing tables already carry, for the
reason their comments give: two sticks holding clones of the same media produce
identical `location` and `analyze_path`, so neither is unique on its own.

Three things this schema kills:
- The `device|playlist path` string-mangling both features use to make playlist
  names globally unique — `medium` is a column now, and lookups use ids.
- The `IS_RECORDBOX_DEVICE` / `IS_NOT_RECORDBOX_DEVICE` sentinel strings stuffed
  into `TreeItem` payloads.
- Two schemas drifting apart, which is how `key_id` came to be missing on one
  side and empty every Camelot-sorted list.

**`deck_history` needs a new bridge field.** `PdbContents` in `prolink-cxx`
does not surface history playlists today, though `prolink-rekordbox` decodes
them (`HistoryPlaylist`). Adding them to the cxx struct is a small, contained
change in `lib/prolink` — the one Rust-side edit this project needs.

## 6. The query catalogue

Every track list. Each produces `(track_id, position)`; `position` carries the
default order, so §9.2's "Default" is just "no sort".

| Level | View SQL (`FROM deck_library WHERE medium=:m` unless stated) |
|---|---|
| All tracks | `SELECT id, id AS position ... ORDER BY id` — pdb order |
| Genre | `... AND genre=:v` |
| Artist → Album → tracks | `... AND artist=:a AND album=:b` |
| Album | `... AND album=:v` |
| Label | `... AND label=:v` |
| Key | `... AND key_id=:v` |
| BPM bucket | `... AND bpm>=:lo AND bpm<:hi` |
| Date added | `... AND date_added=:v` |
| Playlist | `SELECT track_id, position FROM deck_playlist_tracks WHERE playlist_id=:p` |
| Last played | play log `DESC` ∪ `deck_history` `DESC`, deduped on `track_id`, newest wins |
| Search | `... AND (title LIKE :q OR artist LIKE :q OR album LIKE :q)`, rank as `position` |

And the value lists, which are plain queries behind `DeckCategoryModel`:

```sql
-- Genre / Label / Album: value, count, and one cover for the ones that show art
SELECT genre, COUNT(*)                            FROM deck_library
  WHERE medium=:m GROUP BY genre ORDER BY genre;
SELECT album, COUNT(*), MIN(NULLIF(artwork_path,'')) FROM deck_library
  WHERE medium=:m GROUP BY album ORDER BY album;

-- Keys: ordered in C++ by Camelot index, not by text
SELECT key_id, key, COUNT(*) FROM deck_library WHERE medium=:m GROUP BY key_id;

-- BPM: a whole-BPM histogram; bucketing and the density peak happen in C++
SELECT CAST(bpm+0.5 AS INTEGER) AS b, COUNT(*) FROM deck_library
  WHERE medium=:m AND bpm>0 GROUP BY b ORDER BY b;

-- Playlist rows: count, total duration, first four covers
SELECT COUNT(*), SUM(l.duration) FROM deck_playlist_tracks pt
  JOIN deck_library l ON l.id=pt.track_id WHERE pt.playlist_id=:p;
```

Empty values collect into a `—` row (§7.3) by grouping on
`NULLIF(TRIM(genre),'')` and rendering `NULL` last.

## 7. Controls, mapping and skin

### `[Browser]` control group

| Control | Type | Meaning |
|---|---|---|
| `move` | encoder | ±1 per detent, into whatever has focus |
| `select` | push | Activate the selection |
| `back` | push | Pop one level; at level 0, leave browse mode |
| `sort_menu` | push | Open the sort menu (no-op unless a track list is up) |
| `info_toggle` | push | Toggle the info layout (same condition) |
| `level` | read-only | Depth, 0 = sources |
| `in_track_list` | read-only | Drives the SORT LED |
| `sort_column`, `sort_order` | read-only | Drive the SORT LED's colour and brightness |

`[Master],show_library` stays the browse/deck toggle: the skin binds visibility
to it and `track_loaded` already clears it.

### `mixxx_config/TriMixxx.scripts.js`

- `browse()` → `[Browser],move` when the library is up (waveform zoom otherwise,
  unchanged).
- `encoderPush()` → `[Browser],select`. The `GoToItem` / `focused_widget` branch
  goes; there is one focus now (§4.3).
- `back()` → `[Browser],back`.
- `sortKey()` → short press `sort_menu`, long press `info_toggle`, reusing the
  existing `LONG_PRESS_MS` timer shape.
- Delete `sortAdvance`, `sortApply`, `sortClear`, `sortForget`, `sortReset`,
  `SORT_COLUMNS`; `ledSort()` now reads `[Browser],sort_column`.

MIDI addresses do not change. `TriMixxx.midi.xml` changes only where a handler
name does.

### `mixxx_config/TriMixxx_skin/skin.xml`

- `LibraryView` children → a single `<DeckBrowser>`.
- `TopHeader` gains a `visible` connection on `[Master],show_library` with
  `<Not/>` (§4.4).
- `PowerColumn` deleted; `ShutdownOverlay` untouched.
- `<DeckToast>` added to `RootStack` — **first child**, because that stack
  renders in reverse order and the toast must be on top of both views. The
  existing comment above `RootStack` explains why; it is easy to get wrong.
- `LibraryBottomPad`'s 56 px bezel strip stays, inside the browser now.

## 8. Pi-side changes

### 8.1 Volume labels

The name at level 0 must be the stick's own label, not `DJ_USB_1` (§11.2). Two
ways in, and the plan does both because they cost almost nothing together:

**Primary — `pi_config/dj-usb/dj-usb` writes a sidecar.** The mount script
already runs `blkid` for the filesystem type; one more field and one more line:

```sh
label=$(blkid -o value -s LABEL "$dev" 2>/dev/null || true)
mkdir -p /run/dj-usb && printf '%s\n' "$label" > "/run/dj-usb/DJ_USB_$i.label"
```

with the matching `rm -f` in `do_unmount`. `/run` is tmpfs, so it self-cleans.

**Fallback — Mixxx works it out.** No sidecar (a dev box, or a stick mounted by
something else): resolve the mount point to its device through
`/proc/self/mountinfo`, then match it against the `/dev/disk/by-label/*`
symlinks. Empty label → `USB 1` / `USB 2` by slot.

**Not over MIDI.** The daemon route was considered and does not work: a
controller script can only move control *values*, so a string cannot reach the
C++ side through the mapping at all. It would need a new SysEx handler in C++ —
more moving parts than reading a file, to carry a fact that is already on the
local filesystem.

### 8.2 Cache tiers

- A tmpfs mount unit for tier 1, sized 1 GB, at `/run/trimixxx/cache` (§12.2–3).
- Tier 2 under `~/.cache/trimixxx/tracks`, wiped by a `systemd-tmpfiles` rule or
  an `ExecStartPre` on the session.
- Both paths configurable, because §17.1 expects tier 1's size to move once it
  has been measured under load.

Note for the deck's own health: swap is 2 GB of zram under `rpi-swap`
(zram + writeback file). Tier 1 is sized to stay clear of it; the diagnostics
page reports swap-in-use so the assumption is visible rather than assumed.

## 9. Stages

Each stage ends with something demonstrable on the deck. The riskiest work is
first, while the old UI is still there to compare against.

### Stage 0 — the loop (½ day)

The deck runs `scrot`; a screenshot is `ssh` + `scp` away, which means UI work
can be *seen* without photographing a screen. Wrap it:
`pi_config/deck-shot` → grabs `:0`, copies it back, opens it.

Also verify the build loop end to end. The Mac is arm64 and Docker's server is
arm64, so `mixxx/upload.sh` builds **natively**, not under emulation, and the
Dockerfile already cache-mounts `/build`, `/ccache` and the cargo registry — so
an incremental edit is minutes, not an hour. This is better than feared and
needs no change.

**Done when:** `deck-shot` produces a PNG of the current screen, and a one-line
source edit reaches the deck in a couple of minutes.

### Stage 1 — one parser, one table (2–3 days) ⚠ riskiest

No UI change at all. Introduce the schema (§5) and `PdbIngest`; point local
parsing at `read_pdb(path)`; delete `prolinkdbwriter` and
`rekordboxfeature.cpp`'s SQL half; have both features read `deck_library`
through `DeckTrackModel`. Add the history fields to the `prolink-cxx` bridge.

Keep the **existing sidebar UI** working on top of the new tables. That is the
point of doing it first: the old view is the reference, and any regression shows
up as a difference on a screenshot rather than as a mystery later.

**Done when:** the current library view lists the same tracks off the same
stick, with `date_added` and `label` now populated, and `cargo test --workspace`
plus the C++ ingest tests are green.

### Stage 2 — media registry, read on detection (2 days)

`MediaRegistry`, `MediumReader`, `VolumeLabel`, and the `dj-usb` sidecar. Media
are read when they appear, not when they are clicked (§11.1); counts and labels
become available before anything is entered. Artwork prefetch drops to on demand.

**Done when:** plugging a stick populates it with no interaction, the sidebar
shows `SAM1` rather than `DJ_USB_1`, and a remote medium reads itself on
discovery with no modal dialog.

### Stage 3 — the browser shell (4–5 days) ⚠ biggest

`WDeckBrowser`, `DeckListView`, the navigation stack, `[Browser]` controls, the
skin swap, and the touch gestures. Levels 0 and 1 plus a working default-layout
track list and track loading. **The old library view is deleted here.**

**Done when:** the deck browses sources → medium → All tracks → loads a track,
by encoder and by touch, full screen, with the header hidden.

### Stage 4 — categories (3 days)

The query catalogue (§6), value lists with counts, playlists with folders and
stitched artwork, artists → albums → tracks, BPM buckets with anchored tiling
and the density peak, date grouping, last played.

**Done when:** every row of the medium menu leads somewhere correct, and BPM
re-buckets live when ring A1 changes the tempo range.

### Stage 5 — sort and info (2 days)

`WDeckSortMenu`, the two-step flow, global sort persistence, the info layout and
its panel, harmonic key colouring, colour stripes, the new SORT LED behaviour.

**Done when:** sorting by BPM moves the BPM out of the panel and onto the rows,
and a compatible key is green against a playing track.

### Stage 6 — search (2 days)

`WDeckKeyboard`, live ranked results, the encoder-scrolls-results rule.

### Stage 7 — toasts, menu bar, POWER (1–2 days)

`WDeckToast` on the registry's signals; the hover-only menu bar; the `Shut down`
root row wired to the existing overlay; POWER removed from the header.

### Stage 8 — the track cache (3 days)

`TrackCache` with both tiers, pinning, LRU, dwell-triggered prefetch,
load-through-cache, the eject-while-playing toasts. The ProLink download cache
moves off the SD card onto tier 1 at the same time.

**Done when:** a stick is pulled mid-track and playback continues, and the
diagnostics page reports zero bytes written to the card.

*Pull this earlier if a gig is coming: the load-through-cache half is
independent of Stages 4–7 and is the only part of this project that prevents a
live failure.*

### Stage 9 — the serve side (2 days + hardware time)

Whole-file cache on a peer's first read, the phantom medium state, the deferred
clean unmount driven by peers' status packets (§12.5). Ends with a capture
session against the CDJ-2000NXS units, which is what decides the final shape
(§17.2).

### Stage 10 — diagnostics (2 days)

`SystemProbe`, the sparklines, the sections in §14, with `statusHtml()` /
`serveHtml()` folded in.

**Total: roughly four weeks of focused work**, front-loaded with the two risky
stages. Stages 4–7 and 10 are largely independent of one another once Stage 3
lands.

## 10. Testing

The parts worth testing are the pure ones, and conveniently they are also the
parts most likely to be quietly wrong:

| Unit | Test |
|---|---|
| `bpmbuckets` | Widths against hand-computed values at ±6/10/16/WIDE; the ±20 cap; anchoring on a reference; the 5-BPM density peak on a synthetic histogram. |
| `harmonickeys` | The full 24×24 Camelot adjacency matrix, wrap at 12→1 included. |
| `pdbingest` | Ingest `lib/prolink/deckA-usb.pdb` (a real 651-track export already in the tree) and assert counts, a known track's fields, playlist nesting and folder counts. |
| Query catalogue | Against that same ingested fixture: every query in §6 returns the expected ids in the expected order. |
| `volumelabel` | Parse fixture `mountinfo` + `by-label` trees. |
| `trackcache` | Tier promotion/demotion, pinning survives eviction pressure, and **the invariant that matters: with the source still present, eviction writes nothing to tier 2.** |

These run in the existing `unittest` Docker stage. Everything above them —
delegates, gestures, layout — is verified by screenshot on the real panel, which
is the only place the 1024×600 geometry and the touch behaviour are real anyway.

## 11. Risks

**`Library` may not tolerate a skin with no `<Library>` node.** `LegacySkinParser`
only calls `Library::bindLibraryWidget()` from `parseLibrary()`, and
`LibraryControl` null-checks its widget pointer, so it should be fine — but
"should" is doing work in that sentence. *Mitigation:* if it bites, keep a
zero-sized `WLibrary` in the skin and leave it hidden. Cheap, ugly, and it
unblocks.

**Deleting features may not compile.** `Library` holds
`m_pMixxxLibraryFeature` and other code paths reach for playlists and crates.
*Mitigation:* stop *adding* features to the sidebar before deleting their
classes; the two do not have to happen together.

**Stage 1 changes a working path with no UI to show for it.** That is why it
keeps the old view running on the new tables — the regression surface is
visible, on a screenshot, against yesterday's screenshot.

**Track objects still land in Mixxx's own `library` table.**
`BaseExternalPlaylistModel::getTrack()` calls `getOrAddTrack()`, which is why
`ProLinkPlaylistModel` overrides it. With everything playing through the cache
(§12), every track will look like a local file under a cache path, and stale
rows will accumulate across boots. Not a Stage 1 problem; decide it in Stage 8.

**The phantom medium (§12.5) is guessed, not known.** Budgeted as such, built
last, and settled by capture.

## 12. Not doing

- ANLZ parsing stays on Kaitai (§1.2).
- QML. Mixxx's QML skin path is a separate, incomplete application mode in 2.5;
  this is QWidgets, like the rest of the skin.
- Writing anything to a stick. Mounts stay read-only; the deck never adds a
  history playlist of its own to a DJ's media.
- Upstreaming. This is a single-deck appliance UI and deletes nine features
  Mixxx's users depend on.
