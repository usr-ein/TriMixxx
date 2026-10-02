# Plain USB sticks — the plan

A stick with music files on it and no rekordbox export: MP3s at the root, MP3s
in folders, folders in folders. Guest DJs bring them, and until now the deck
showed nothing at all for one.

Status (2026-10-02): **the local half is built and tested on unit 2** (§3),
uncommitted. The network half waits on a capture of how a real CDJ shares a
plain stick (§5). Read §1 before touching code.

---

## 1. Decisions

Settled with Sam on 2026-10-01/02.

| # | Question | Decision |
|---|---|---|
| D1 | Where analysis lives | **On the SD card, not in RAM** — RAM is reserved for the track cache. It is **wiped on reboot**: the first Mixxx start after a boot purges the analysis of every session copy. A Mixxx restart within the same boot keeps it. |
| D2 | What gets analysed | **Only the track loaded on the deck**, from the moment it is loaded. Nothing else: no analysis of the selected row, no background pass over a folder. Loading another track, or ejecting, cancels the analysis of the previous one. |
| D3 | Musical key | **Tags only.** Key detection stays off (`[Key] KeyDetectionEnabled 0`). |
| D4 | Formats offered to the network | MP3, M4A (AAC only), WAV, AIFF. No FLAC. (For §5.) |
| D5 | Sharing a plain stick with CDJs | **Deferred.** Sam captures how a real CDJ shares a non-rekordbox stick with another CDJ, and we mimic that. We do **not** pass a folder stick off as a rekordbox medium. |
| D6 | Plain sticks in *other* players | **Deferred**, with D5: the same capture shows the FOLDER browse we would consume. |
| D7 | What a dumb stick looks like | Files anywhere: at the root, at any depth, beside subfolders. One mapping rule (§4). |

## 2. Where we started

Traced on 2026-10-01:

1. `pi_config/dj-usb` mounts **any** USB filesystem read-only at
   `/media/DJ_USB_N`, so the stick is mounted.
2. `MediaRegistry::findLocalMountPoints()` only kept mounts that had
   `PIONEER/rekordbox/export.pdb`, so the stick was dropped silently: no row,
   no toast, nothing to play.
3. A file that fails to decode opens a modal `QMessageBox`
   (`basetrackplayer.cpp`), which blocks a deck that has no mouse.
4. A local medium is keyed by its mount point. A second stick in the same port
   could therefore be handed the first stick's cached copy of a file with the
   same path, together with its beats.

## 3. The local half — what is being built

The goal: plug in a stick of loose files and a SOURCES row appears; the stick
browses by **Folders** (plus All tracks, Artists and the other categories,
filled in from tags); a track loads and plays from RAM; the loaded track — and
only that one — is analysed, with the waveform drawn as it goes and the grid
landing when the analysis finishes. Nothing modal, nothing silent.

| # | What | Where | Status |
|---|---|---|---|
| L1 | `dj-usb` writes `/run/dj-usb/DJ_USB_N.uuid` beside `.label`, and removes it on unmount | `pi_config/dj-usb/dj-usb` | Done — on unit 2 the sidecar is written, and both sidecars go on unmount |
| L2 | A local medium's key includes its volume UUID: `usb:<mount>#<uuid>`. The same mount with a different UUID is an eject followed by an insert | `mediumid.h`, `volumelabel.{h,cpp}`, `mediaregistry.cpp` | Done — swapping PLAINTEST for SWAPTEST in one go logged `a different stick is now at`, and the same path played the new stick's audio from a new copy, analysed afresh (128 BPM, not the old 145) |
| L3 | Detection keeps every real mount under `/media`. A pdb that parses is read as today; no pdb, or one that fails to parse, is read as a **Folder** medium; no music at all is **Failed** with `no music found` | `mediaregistry.cpp` | Done — 14 files read in 14–36 ms, 2 000 in 426 ms; `no music found` seen with DOCSONLY; unreadable pdb: BADPDB |
| L4 | `folderlibrary.{h,cpp}`: pure, no I/O. It turns a list of files into the `PdbContents` that `writeMedium()` already ingests (§4) | `src/library/deck/` | Done — unit-tested |
| L5 | `folderscan.{h,cpp}`: pass A walks the stick and builds the library, after which the medium is browsable; pass B reads tags in chunks of 40 files and updates the rows in place. Chunks give way to new media and to track copies | `src/library/deck/`, `pdbingest.cpp` (`updateTrackTags`) | Done — pass B over 2 000 files takes under 6 s; untagged WAV/MP3 keep their durations |
| L6 | Browser: **Folders** in place of Playlists, directly after Search; the source row reads `N tracks · M folders`; folder rows show `N items`; the list on screen refreshes in place as tags arrive; the analysed BPM is written back to the row | `wdeckbrowser.cpp` | Done — counts, plurals and in-place menu refresh seen (Genre 8→7, Artists 51→50 as tags landed); BPM written back after a fresh analysis (WAV 124.8, AIFF 134.0, M4A 138.0) and at load for one already analysed (128.0) |
| L7 | Toasts: `KINGSTON inserted — browsing folders`; `— no music found`; `— rekordbox library unreadable, browsing folders`. A failed load shows `Couldn't load …` **instead of the modal dialog** | `wdecktoast.cpp`, `basetrackplayer.{h,cpp}`, `legacyskinparser.cpp` | Done — insert, no-music and load-failure toasts seen; a load pressed twice shows one toast; a stick's later toast replaces its insert toast |
| L8 | D2: one analysis worker; a different track loaded, or an eject, **replaces** the scheduler (`stop()` is one-shot); analysis threads run at nice +10 | `playermanager.{h,cpp}`, `analyzerthread.cpp` | Done — one worker at nice 10; a new load abandons the old analysis |
| L9 | D1: on the first start after a boot (`/proc/sys/kernel/random/boot_id` changed), purge Mixxx's library rows for tracks under the RAM store, the track cache's disk tier and `/media`. That removes their beats, cues and waveform files | `sessionpurge.{h,cpp}`, `wdeckbrowser.cpp` | Done — three same-boot restarts kept the analysis; the first start of a boot purges |
| L10 | Announce nothing to the network for a local medium we are not serving (a folder medium is never served) | `mediaregistry.cpp` | Done — needs a second player on the network to observe |
| L11 | Unit tests for the mapping, the filename guesses and the natural order | `src/test/folderlibrary_test.cpp` | Done — 18 tests pass (`FolderLibrary*`, `FolderFileNames*`, `PlayerManager*`) |
| L12 | Rows changed after they were first shown — tags, a written-back BPM — are re-read into the browser's track cache, which otherwise keeps its first copy of a row until Mixxx restarts | `wdeckbrowser.cpp`, `mediaregistry.{h,cpp}` | Done — found on the deck; see below |
| L13 | A failed load or an eject takes the loaded marker off its row | `wdeckbrowser.{h,cpp}`, `legacyskinparser.cpp` | Done |

**Found while testing, and fixed:**

- **FAT sticks mangled accented names.** The Pi kernel mounts vfat with
  `iocharset=ascii`, so `Zøe`, `Ø [Phase]` and the like could not be opened by
  their real name. That applies to a **rekordbox FAT32 stick as well**: its
  pdb lists such a file fine, and then the file will not load. `dj-usb` now
  mounts vfat with `utf8`.
- **The browser's track cache never re-reads a row.** It is filled lazily as
  rows are shown, and after that only a restart refreshed it. A list opened
  before a stick's tags arrived kept the file names, and a written-back BPM
  never showed. Fixed as L12.
- A folder track's `analyze_path` was the bare mount point, so the deck tried
  to read `/media/DJ_USB_1` as an ANLZ file. It is now left empty.

**Known gaps, not addressed here:**

- SOURCES lists sticks in the order they went in, not by slot. That is how it
  already was for rekordbox sticks.
- On an eject or a failed load, the network keeps being told the old track is
  loaded. `announceNothingLoaded()` is never called on either. Left for the
  network half.
- Unit 2 logs audio underflows at start-up and at each load. That was already
  the case before this work, and fits F12 below: nothing runs at realtime
  priority.

**Testing on the deck** (unit 2, `HOST=trimixxx-pi-2`; unit 1 is away):

- Build a FAT32 image of tracks from `~/Music/tracks`, laid out like Appendix B.
- Attach it with `losetup` and mount it with `sudo dj-usb mount loopN`. That
  goes through the real mount path, sidecars included.
- Check the browse, a load of each format, the analysis, the log, and a stick
  pull mid-track (`dj-usb unmount`).

The test images on unit 2, in its home directory. Insert one as a stick with
`dev=$(sudo losetup -f --show ~/fakeusb.img) && sudo dj-usb mount ${dev#/dev/}`,
and pull it with `sudo dj-usb unmount loopN && sudo losetup -d /dev/loopN`.
Detach them all when done: two slots, and a real stick finds no free one.

| Image | Label | What it tests |
|---|---|---|
| `~/fakeusb.img` | PLAINTEST | Appendix B: every format, nesting, junk, a corrupt file |
| `~/swaptest.img` | SWAPTEST | another track at PLAINTEST's `Techno/Dax J - Anti Gravity Racing.mp3` |
| `~/bigstick.img` | BIGSTICK | 2 000 tagged files in 20 folders, named differently from their tags |
| `~/docsonly.img` | DOCSONLY | no music at all |
| `~/badpdb.img` | BADPDB | a garbage `export.pdb` beside two MP3s |

**Rebuild and deploy:** `pi_config/dj-usb/install.sh` for L1; then
`HOST=trimixxx-pi-2 mixxx/upload.sh` for the binary. Unit tests:
`docker buildx build --platform linux/arm64 --target unittest --build-arg BASE=debian:trixie --build-arg GTEST_FILTER='FolderLibrary*:FolderFileNames*:PlayerManager*' mixxx`.
Do not edit sources while either build runs. A file edited after the build
has copied the tree is compiled from the old copy, and its object comes out
newer than the edit, so later builds never redo it. `touch` does not help:
BuildKit caches the copy by content, and the new mtime never reaches the
container. If it happens, prune that tree's cache mount (`docker buildx du
--verbose` gives its id) and keep `/ccache`.

## 4. The folder → library mapping

### 4.1 What is music

- **Extensions:** `mp3 m4a mp4 aac flac wav aif aiff ogg opus` that Mixxx can
  decode.
- **Skipped:** names starting with `.`, which catches macOS's `._*` AppleDouble
  files; zero-byte files; and the directories `System Volume Information`,
  `$RECYCLE.BIN`, `RECYCLER`, `LOST.DIR`, `PIONEER` (a CDJ writes
  `MYSETTING.DAT` there even on a plain stick), `Engine Library` and `_Serato_`.
  `Contents/` is kept, because on a Device-Library-Plus-only stick that is
  where the audio is.
- **Caps:** depth 16 and 20 000 files. Past either, show what was found and log
  it.

### 4.2 The tree

rekordbox's model, which the browser speaks, has folders that hold nodes and
playlists that hold tracks. A directory becomes:

- a **playlist** named after it, if it holds music only directly;
- a **folder** named after it, if any subdirectory holds music. Its children
  are those subdirectories, plus — when it also holds music directly — a
  playlist **of the same name** holding just those files.

The stick's own top-level files become a top-level playlist named after the
**volume label**. Directories with no music anywhere below them are left out.
Siblings that would share a name get ` (2)`, ` (3)`.

```
on the stick                    Folders
/a.mp3                          ▸ House            folder
/b.mp3                              House          playlist: c.mp3
/House/c.mp3                        Deep           playlist: d.mp3
/House/Deep/d.mp3               KINGSTON           playlist: a.mp3, b.mp3
/Techno/e.mp3                   Techno             playlist: e.mp3
/Empty/                         (left out)
```

**Order.** Folders come before playlists, because that is how the browser
lists them. Within each group, a directory's own-files playlist comes first,
then the rest in natural, case-insensitive order (`2` before `10`). Tracks are
in natural filename order.

**Ids.** `rb_id` counts 1..N in a depth-first walk: a directory's own files
first, then its subdirectories, in the order above. Playlist ids count 1..M in
the same walk. The stick is mounted read-only, so an unchanged stick always
gets the same ids.

### 4.3 Track fields

- **Pass A** (the walk):
  - The title is the filename stem. A leading `NN - `, `NN. ` or `NN ` (two or
    more digits) becomes the track number. The **first** ` - ` splits off the
    artist, so `Underworld - Bruce Lee - Ricks 1st Dobro Mix` gives artist
    "Underworld" and title "Bruce Lee - Ricks 1st Dobro Mix".
  - Date added comes from the file's mtime.
  - Artwork is the first of `cover|folder|front|album|artwork`
    `.jpg|.jpeg|.png` in the track's own folder.
- **Pass B** (tags, through
  `SoundSourceProxy::importTrackMetadataAndCoverImageFromFile`, with no cover
  decoded):
  - Fields: title, artist, album, genre, year, label, comment, track number,
    duration, bitrate, sample rate.
  - BPM, kept only between 40 and 300.
  - Key, as text (D3).
  - A field is only overwritten when the tag actually has a value.

## 5. Facts the network half will need

Kept from the research of 2026-10-02 so it does not have to be redone.

- **F1 — The MP3 seek index (PVBR) is 400 zero words and a sample count.**
  - A CDJ will not read an MP3 it was not given a `0x2504` answer for.
  - All 741 `0x4502` replies in the corpus have one of two shapes: 716 are 400
    zero words and a final word equal to the track's total sample count; 25
    (non-MP3) are all zero.
  - The count is the frame count in the Xing/`Info` header × 1152 (MPEG-1) or
    576 (MPEG-2). Appendix A checks this exactly on three files.
- **F2 — A CDJ cannot be listed over NFS** (`READDIR` → `PROC_UNAVAIL`,
  `consume/nfs.rs:111`). A plain stick in a CDJ is only reachable through its
  dbserver **FOLDER** menu.
- **F3 — FOLDER is half-known.**
  - The request is `0x2006 [r:m:s:t (track type 2), sort, folder id
    (0xffffffff = root), 0]`.
  - A folder row is `[parent, id, len, name, len, "", 0x0001, 0…]`; both are
    confirmed in `S20` and `S15b`.
  - `DbClient::folder()` already sends it.
  - **File rows, and what a CDJ does to load an unanalysed file, are in no
    capture.** That is Sam's capture.
- **F4 —** An empty beat grid is a documented protocol state, but no capture
  shows a CDJ loading a track that has one.
- **F9 —** `MediaInfo::isOccupied()` (`trackCount > 0 || !name.isEmpty()`)
  would hide an unlabelled plain stick in a CDJ.
- **F11 —** Our other deck ingests remote media by pulling `export.pdb` over
  NFS; a plain stick has none.
- **F12 —** Realtime audio priority on the decks is unknown
  (`fresh-install.md` §1.6).

### What Sam's capture should contain

Two CDJ-2000NXS and the Mac bridge. A plain stick (FAT32, no `PIONEER/`, files
at the root and in nested folders, a WAV/AIFF/M4A and a VBR MP3) goes in deck
A. Then:
1. On deck B: LINK → A → USB. Note the root menu.
2. FOLDER → into two levels and back.
3. Load a root MP3, a nested WAV and the VBR MP3; play each and press INFO;
   needle-search the VBR one.
4. Run `prolink media` once.

It answers: the root menu for a plain stick, the shape and meaning of a file
row, the requests a load makes (`0x2202`? `0x2102` with type 2? `0x2504`?), the
NFS path read, the track type and id published, and the media-query counts.

---

## Appendix A — the PVBR evidence

| File | Header frames | Samples per frame | Product | Captured final word |
|---|---|---|---|---|
| `format-matrix/03 MP3 MPEG1 128k 44k1.mp3` | 16 271 (`Info`) | 1152 | 18 744 192 | S13: **18 744 192** |
| `6 SENSE - Mechanical Mania.mp3` (48 kHz) | 17 710 (`Info`) | 1152 | 20 401 920 | S10h: **20 401 920** |
| `format-matrix/20 MP3 MPEG2 32k 24k.mp3` | 17 711 (`Info`) | 576 | 10 201 536 | S11, S18: **10 201 536** |

A frame walk finds one more frame than the header states: the `Info` frame
itself, which is not counted.

## Appendix B — the test stick

```
/Track At Root.mp3                 a root file
/01 - Some Artist - Some Title.mp3 filename parsing
/cover.jpg                         root artwork
/House/a.mp3                       a mixed directory: files…
/House/folder.jpg
/House/Deep/b.flac                 …and a subfolder
/House/Deep/c.wav
/Techno/Peak Time/d.aiff           deep nesting
/Techno/Peak Time/ünïcödé.m4a      a non-ASCII name
/Empty Folder/                     left out
/Only Docs/readme.txt              left out
/._Track At Root.mp3               AppleDouble junk: skipped
/corrupt.mp3                       random bytes: the load fails with a toast
/zero.mp3                          0 bytes: skipped
/PIONEER/MYSETTING.DAT             a CDJ's settings on a plain stick: still Folder
```
