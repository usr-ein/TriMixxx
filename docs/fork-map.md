# The fork's map

What the TriMixxx fork of Mixxx adds to upstream Mixxx 2.5.6, where each part
lives, who owns what, and where the fork reaches into upstream's own files.

**Status: a plan.** This is the map of the fork *as the `fork-refactor` branch
will leave it*. The second half, [The refactor](#the-refactor), is the plan
that gets it there; it goes once the refactor is done, and the map above it
stays as the fork's map, kept true by whoever changes the structure next.

The fork is upstream 2.5.6 (`3ebac449e7e5`, "Release 2.5.6") plus the
fork's own commits. Measured at `792027c`, the fork adds 119 files under
`mixxx/src` (26.4k lines, tests included) and changes 57 upstream files there
(+1.7k / -0.2k lines).

---

## At a glance

Four layers, each using only the ones below it:

```
 widget/deck/  (+ widget/wtempopanel, widget/wprolinkphasemeter)    what the DJ sees
     │  WDeckBrowser, WDeckToast, WDeckDiagnostics, the delegates...
     ▼
 library/deck/                                                       the deck's library and services
     │  DeckServices (the one owner), MediaRegistry, DeckLoader,
     │  TrackCache, RemoteTrackStreamer, DeckAutoplay, PdbIngest...
     ▼
 library/rekordbox/            network/prolink/                      the two things a deck reads from
   rekordbox's files:            Pro DJ Link: the network
   export.pdb, ANLZ, and         session (Rust), tempo sync,
   applying them to a Track      KEY SYNC, the [ProLink] controls
     │                              │
     └──────────────┬───────────────┘
                    ▼
            lib/prolink (Rust, linked statically)
```

`sources/soundsourcestreaming` (a decoder for a file still arriving) uses
`library/deck`'s `StreamingFile`. Upstream files include the fork's headers in
five places only: `CoreServices` makes `DeckServices`, `main.cpp` installs the
signal handler, `MixxxMainWindow` hands its menu bar to `MenuBarReveal`,
`SoundSourceProxy` registers the streaming decoder, and `WaveformRendererRGB`
draws through `StackedWaveform`. `LegacySkinParser` reaches the deck's skin
nodes through one member defined in a fork file.

---

## Where things live

### `src/network/prolink/`: Pro DJ Link, and nothing else

Usable and testable without a Library: nothing here includes `library/` or
`widget/`.

| File | What it is |
|---|---|
| `prolinknetworkservice.{h,cpp}` | `ProLinkNetworkService`: the Qt shell around the Rust session. Starts and stops it, polls it every 33 ms on the GUI thread, mirrors its devices, media and serve status as signals, runs the transfers (a file, a file streamed head first, a database, a cover, a preview waveform), says which player number we hold. Drives `ProLinkSync` each poll. |
| `prolinksync.{h,cpp}` | `ProLinkSync`: tempo sync, as a CDJ does it. Who is tempo master (reconcile, stand down, auto-claim, hand over), what we tell the network we play (tempo, beat, loaded track), SYNC following the master (tempo, then the phase: landing, hold, trim), and the deck the phase meter draws. `docs/tempo-sync.md` is its specification. |
| `syncsource`, `synctempo`, `automaster` | The pure rules `ProLinkSync` decides with: what SYNC follows and what the meter draws, fader or master, when to claim or hand over master. Tested without a network. |
| `audiblebeatclock`, `prolinkbeatposition.h` | Where this deck is on its grid *as heard*, numbered from the rekordbox downbeat. Used by `ProLinkSync` and the phase meter. |
| `keysync.{h,cpp}` | KEY SYNC's rules: a latch on a key, never let go by the network. |
| `prolinkkeysync.{h,cpp}` | `ProLinkKeySync`: KEY SYNC wired to the deck (`pitch_adjust`, `file_key`, `keylock`) and the controls. |
| `prolinkcontrols.{h,cpp}` | `ProLinkControls`: the `[ProLink]` controls. Made before any skin is parsed, because the first creator of a control key wins (see the class). |
| `prolinktypes.h` | The values that cross from the service to the rest of Mixxx: `ProLinkDevice`, `MediaInfo`, `MediaSlot`. |
| `prolinkservestatus.h` | `ServeStatus`: what we offer the network and who is reading it. |

### `src/library/rekordbox/`: rekordbox's files

| File | What it is |
|---|---|
| `rekordboxpdb.{h,cpp}` | Reading `export.pdb` (through `lib/prolink`): tracks, playlists, history. A folder stick is described in the same types. |
| `rekordboxanlz.{h,cpp}` | Reading an analysis file, `.DAT` or `.EXT` (through `lib/prolink`): grid, cues, waveforms, previews. |
| `rekordboxanalysis.{h,cpp}` | Applying one to a `Track`: beat grid, hot cues, loops, memory cues, in frames. |
| `rekordboxwaveform.{h,cpp}` | Building Mixxx's waveforms from rekordbox's colour waveform, so a track is not decoded to be drawn. |

### `src/library/deck/`: the deck's library and services, no widgets

| File | What it is |
|---|---|
| `deckservices.{h,cpp}` | `DeckServices`: the one owner of everything below that lives as long as Mixxx does, and of the network side. Made by `CoreServices` after the library and before the skin; the skin's deck widgets are handed it. See [Ownership](#ownership-and-lifetime). |
| `mediaregistry.{h,cpp}` | `MediaRegistry`: every medium the deck can play from (local sticks, other players' slots), read as soon as it appears; toasts and the browser's level 0 come from its signals. Turns network events into media, asks for remote covers and previews, says what the deck has loaded in the network's terms, and resolves the tempo master's key for KEY SYNC. |
| `remotetrackstreamer.{h,cpp}` | `RemoteTrackStreamer`: a remote track played while it arrives. Fetches its grid first, then streams the audio head first into the track cache, and keeps the `StreamingFile` told which ranges landed. |
| `deckloader.{h,cpp}` | `DeckLoader`: a `deck_library` row onto the deck. A local copy or a stream, never the medium; the `Track` filled in from the pdb (metadata, key, cover, rekordbox analysis); a folder track's BPM written back; announced to the network; this boot's play log. The browser and autoplay both load through it. |
| `trackcache.{h,cpp}` | `TrackCache`: the deck plays copies, never the medium. Copies a stick's track while it plays, RAM first, the card only for what can no longer be re-read. |
| `streamingfile.{h,cpp}` | `StreamingFile`: a file still arriving that a decoder reads as if it were not; a read of a hole waits. `StreamingFileRegistry`: which paths are such files. |
| `ramstore.{h,cpp}` | Where RAM-backed scratch goes, and how much of it there is. |
| `pdbingest.{h,cpp}` | The `deck_*` tables, and the one thing that writes them: a pdb (or a folder stick) into SQL; tags into rows. |
| `deckqueries.{h,cpp}` | The whole browse hierarchy as SQL: every track list, every category, BPM buckets, harmonic keys. |
| `decktrackmodel.{h,cpp}` | `DeckTrackModel`: every track list the browser shows, one temporary view each. |
| `folderlibrary.{h,cpp}`, `folderscan.{h,cpp}` | A stick with no rekordbox library, turned into one: the pure mapping, and the disk walk and tag reads. |
| `mediumid.h` | `MediumId`: which medium, as a key that survives SQL. Also parses its own Pro DJ Link address back out. |
| `volumelabel.{h,cpp}` | What a stick calls itself, and its UUID. |
| `sessionpurge.{h,cpp}` | Forgetting, once per boot, what Mixxx's own library stored about the session's copies. |
| `previewwaveform.{h,cpp}`, `previewwaveformcache.{h,cpp}` | The info panel's preview waveform, from a stick's ANLZ or a player's reply, read off the GUI thread. |
| `autoplay.{h,cpp}` | Autoplay's rule and memory: the nearest BPM among a genre's unplayed tracks, in rounds. Pure. |
| `deckautoplay.{h,cpp}` | `DeckAutoplay`: the rule driven on the deck, track after track, through `DeckLoader`. |
| `camelot.h` | Camelot order and compatibility. |

### `src/widget/deck/`: the deck's widgets

| File | What it is |
|---|---|
| `wdeckbrowser.{h,cpp}` | `WDeckBrowser` (skin node `<DeckBrowser>`): the library as a menu stack. Levels, breadcrumb, sort, search, the info layout, and the `[Browser]` controls. Loads through `DeckLoader`. `DeckSortChip`, the breadcrumb's sort indicator, is declared beside it. |
| `decklistview`, `deckdelegates`, `deckmenumodel`, `deckpage.h` | The lists: one view class with the deck's gestures, the row painters, the menu rows, and the page interface for a level that takes the encoder itself. |
| `wdeckinfopanel`, `wdecksortmenu`, `wdecksearch` | The info panel, the sort menu, the on-screen keyboard. |
| `wdeckdiagnostics`, `decklevels`, `deckrelease` | Diagnostics, with the output level and backlight it adjusts and the release and A/B slots it reports. |
| `wdecktoast` | `<DeckToast>`: the notification stack over everything. |
| `wdeckautoplaybadge` | `<DeckAutoplay>`: autoplay's badge over the waveform. |
| `menubarreveal.{h,cpp}` | `MenuBarReveal`: the main window's menu bar, hidden, and shown only while a real mouse hovers the top edge. |
| `deckaccent.h`, `deckbezel.h` | The accent colour and the bezel, as the TriMixxx skin declares them, for widgets that paint themselves. |

Beside them: `widget/wtempopanel` (`<TempoPanel>`) and
`widget/wprolinkphasemeter` (`<ProLinkPhaseMeter>`), the deck view's own.

### Elsewhere

| File | What it is |
|---|---|
| `skin/legacy/legacyskinparser_deck.cpp` | `LegacySkinParser::parseDeckNode()`: builds the deck's five skin nodes. A member of the upstream parser defined in a file of the fork's, so upstream's `parseNode()` carries one line for all of them. |
| `sources/soundsourcestreaming.{h,cpp}` | `SoundSourceStreaming`: FFmpeg fed from a `StreamingFile`, for a track still arriving off a stick or a player. Declines every other file. |
| `waveform/renderers/stackedwaveform.{h,cpp}` | `StackedWaveform`: the scrolling waveform's `<SignalStacked>` look (bass, mids, highs as stacked bars), drawn for `WaveformRendererRGB`. |
| `util/posixsignalhandler.{h,cpp}` | SIGTERM, SIGINT and SIGHUP as a normal quit. |

---

## Ownership and lifetime

```
CoreServices                                    (upstream; owns one fork member)
 └─ DeckServices          made at the end of CoreServices::initialize(), before
     │                    the skin and the controllers; gone first in finalize(),
     │                    after the skin
     ├─ ProLinkControls          the [ProLink] controls
     ├─ the deck_* tables        dropped and made again; the boot purge
     ├─ TrackCache
     ├─ ProLinkNetworkService    the Rust session ─┐
     │    └─ ProLinkSync                           │ the deck group
     ├─ ProLinkKeySync                             │ ("[Channel1]") is
     ├─ MediaRegistry                              │ named once, here,
     ├─ RemoteTrackStreamer                        │ and handed down
     ├─ DeckLoader                                 │
     └─ DeckAutoplay           ────────────────────┘

the skin (rebuilt on a skin reload; the services above are not)
 ├─ WDeckToast, WDeckAutoplayBadge     handed the services by parseDeckNode()
 └─ WDeckBrowser                       handed the services; owns its views,
      ├─ DeckTrackModel, the delegates   models, PreviewWaveformCache, the
      ├─ WDeckDiagnostics                [Browser] controls and Diagnostics
      └─ PreviewWaveformCache
```

**Why not the browser.** Until this refactor the browser widget owned the
registry, the cache, autoplay, and through the registry the Pro DJ Link
session, tempo sync and KEY SYNC. Widgets built before it (the toast, the
autoplay badge) had to wait for it through `whenReady()` queues, three classes
grew an `instance()`, the `[ProLink]` controls moved to `CoreServices` to
escape the skin's creation order, and a skin reload rebuilt the network
session. One owner made before the skin ends all four.

`DeckServices::instance()` exists for one caller, `parseDeckNode()`, which
hands the services to the widgets it builds. Nothing else reaches for it.

## Threads

Everything above runs on the GUI thread except:

- **the Rust session**, on its own runtime; the GUI thread only drains it
  (`ProLinkNetworkService::poll()`, every 33 ms);
- **reads of a medium** (`MediaRegistry`: pdb ingest, folder walks and tags),
  on Qt's pool, one at a time;
- **copies off a stick** (`TrackCache`), on a pool of one thread of its own;
- **preview waveforms** (`PreviewWaveformCache`), on a pool of one of its own;
- **`StreamingFile::read()`**, on the decoder's thread, where a read of a range
  not yet there waits; it refuses to wait on the GUI thread, which is the
  thread that announces arrivals;
- **KEY SYNC's keylock watcher**, on whichever thread toggled keylock;
- **the engine**, which sees the fork only as `phase_trim` (`RateControl`)
  and a decoder (`SoundSourceStreaming`).

---

## The fork inside upstream files

What stays in upstream files, and why it cannot be elsewhere. Rebasing onto a
newer Mixxx means replaying these.

| Upstream file | What the fork does there |
|---|---|
| `CMakeLists.txt`, `cmake/modules/ProlinkRust.cmake` | The fork's sources; `lib/prolink` linked statically. |
| `library/rekordbox/rekordboxfeature.{h,cpp}`, `lib/kaitai/`, `lib/rekordbox-metadata/` | Deleted: the deck reads sticks itself, through `lib/prolink`, and the Kaitai parsers went with the feature. `.pre-commit-config.yaml` loses their exclusion. |
| `coreservices.{h,cpp}` | Makes and drops `DeckServices`. Never writes `mixxx.cfg`. `--log-path`. |
| `util/cmdlineargs.{h,cpp}`, `dialog/dlgdevelopertools.{cpp,ui}` | `--log-path`, and the log found there; a "ProLink only" filter. |
| `main.cpp` | Installs `PosixSignalHandler`. |
| `mixxxmainwindow.cpp` | Hands the menu bar to `MenuBarReveal`. |
| `skin/legacy/legacyskinparser.{h,cpp}` | One `parseNode()` branch and one declaration: `parseDeckNode()`. |
| `library/library.cpp`, `preferences/dialog/dlgpreflibrary.{cpp,ui}` | Upstream's rekordbox feature is gone (the deck reads sticks itself); no save of `mixxx.cfg`. |
| `library/trackmodel.h`, `library/basesqltablemodel.{h,cpp}` | `clearSorting()`, the browser's Default sort. |
| `mixer/playermanager.{h,cpp}` | Only the deck's own track is analysed, one at a time, and a track rekordbox analysed is not analysed again; a double tap with no deck to clone loads. |
| `mixer/basetrackplayer.{h,cpp}` | A failed load is a signal (the toast says it), not a modal dialog; cloning says whether it cloned. |
| `engine/controls/ratecontrol.{h,cpp}` | `phase_trim`, SYNC's phase hold inside the engine. |
| `engine/cachingreader/cachingreaderworker.{h,cpp}` | A load replaced while it opened fails silently instead of ejecting its successor. |
| `analyzer/analyzerthread.cpp` | Analysis runs at nice 10. |
| `track/track.cpp` | The BPM follows a grid set before the duration was known. |
| `util/db/dbconnection.cpp` | WAL, so a stick's ingest does not block the browser. |
| `sources/soundsourceffmpeg.{h,cpp}` | A hook for a custom AVIOContext; no seek before the stream starts. |
| `sources/soundsourceproxy.cpp` | Registers `SoundSourceStreaming`; never hands a file still arriving to another decoder. |
| `waveform/visualplayposition.{h,cpp}` | `getAudibleOffsetNow()`, the position as heard. |
| `waveform/renderers/waveformwidgetrenderer.cpp` | A constant scale, not compressed by tempo. |
| `waveform/renderers/waveformrenderbeat.{h,cpp}` | `<DownbeatColor>` and `<BeatWidth>`. |
| `waveform/renderers/waveformrendererrgb.{h,cpp}` | `<SignalStacked>`, drawn by `StackedWaveform`; why nothing was drawn, logged. |
| `widget/woverview.{h,cpp}` | `<SignalStacked>` for the Filtered overview; the summary drawn once the engine knows the track's length. |
| `widget/wkey.{h,cpp}` | `<Notation>`. |
| tests | `enginebuffertest`, `playermanagertest`, `trackupdate_test`, and `autodjprocessor_test`'s mock, for the above. |

---

## The refactor

Pitch, Sam's words: *"refactor my mixxx fork to make sure the structure of
the C++ code makes sense, is not overengineered, has no dead code, is well
split up, applies best engineering C++ practices but also is not applying
design patterns just to look smart."* Settled with him: the fork's own code
and its patches; **no behaviour change** (bugs found go to follow-ups);
upstream's own code left alone so a rebase stays possible; a plan review,
then a result review.

Everything below this line goes once the refactor is done.

### What the survey found

**Dead code, whole.**
- `library/prolink/`: `ProLinkFeature` (the old sidebar), `ProLinkPlaylistModel`,
  `ProLinkTrackFetcher`, `DlgProLinkFetch`, `ProLinkDbWriter`: 10 files, 2,174
  lines. Built only when `[Library] ShowProLinkLibrary` is set, and every
  deck's `mixxx.cfg` has it at 0. It was also a second Pro DJ Link session: the
  "one session per process" guard in the service exists because of it.
- With it: the `PROLINK` CMake option and `__PROLINK__` (no build turns it off,
  and `lib/prolink` is linked unconditionally anyway), 40 lines of `#else`
  stubs in `MediaRegistry`, three sidebar icons, and `TrackModel::willLoadTrack()`
  (only `ProLinkPlaylistModel` overrode it).
- Declared and never defined, or never used: the `ProLinkMediaQuery` class and
  its two functions; nearly all of `prolinkdefs.h` (ports, packet offsets,
  handshake and keep-alive timings, `PacketType`, a second set of player-number
  constants), left from the C++ protocol `lib/prolink` replaced;
  `ProLinkDevice`'s timers, `macString()` (never defined), `sameDeviceAs()`,
  `isPlayer()`, `label()`, `nameRaw`, `interfaceName`, `kind`;
  `ServeStatus`'s counters and a consumer's title and artist;
  `ProLinkNetworkService::isListening()`; `PdbTrack::analyzeExtPath()`;
  `rekordbox::colorFromID()`; `StreamingFileRegistry::count()`;
  `PresentRanges::availableFrom()` (tests only); `PreviewWaveformCache::count()`
  and `bytes()`; `WDeckBrowser`'s `CategorySpec`; a ternary whose branches are
  the same; `pdbingest`'s "All tracks" playlist row (rb_id 0), written for the
  old sidebar and filtered back out by both queries that could see it.

**Patches the deck never reaches.** The deck's skin has no `<Library>`, no
sidebar and no track table; its mapping sends neither `[Library],GoToItem` nor
`sort_reset`; its overview is the Filtered one (`WaveformOverviewType 0`).
So these only change what a stock skin would do, and go back to upstream:
`WLibrarySidebar` (sizing and sideways scroll, 118 lines), `WLibrary` (focus on
show), `WTrackTableView` and `TrackModel` (`willLoadTrack`), `TreeItem`
(`takeChildren()`, no caller), `LibraryControl` (a GoToItem guard and
`[Library],sort_reset`), `Library`'s sidebar default selection (a no-op since
the rekordbox feature left the top of the list), and `WOverview`'s one-sided
RGB overview. 10 upstream files go back to upstream as a result.

**One owner missing.** See [Ownership](#ownership-and-lifetime): the browser
widget owns the deck's services, and three singletons, two `whenReady()`
queues and the controls' move to `CoreServices` work around it.

**Three classes doing several jobs.**
- `ProLinkNetworkService` (2,050 lines): the session's Qt shell *and* the whole
  tempo sync engine, ~25 of its members and ~650 lines, though its own header
  says it is "the Qt shell ... and does nothing else".
- `WDeckBrowser` (2,590 lines, a 475-line constructor): the view *and* the
  owner of every deck service *and* the load path (`loadRow()` alone is 170
  lines), which autoplay borrows through a callback.
- `MediaRegistry` (1,900 lines): the media *and* streaming a remote track
  (~300 lines, five members of its own) *and* owning KEY SYNC.

**Things in the wrong place.** `export.pdb` and ANLZ readers in
`network/prolink/` (a plain folder stick is built as a
`mixxx::prolink::PdbContents`); `DeckAutoplay`, a controller, in `widget/deck/`;
`SoundSourceProLink` decodes local stick copies too; a one-file
`network/prolink/server/`; `MediumId`'s key format parsed by hand in three
places of `MediaRegistry`; `"[Channel1]"` defined in three files.

**Fork logic inside upstream files that a hook can hold.** `LegacySkinParser`
(five parse functions, 97 lines, for five skin nodes), `MixxxMainWindow` (the
menu bar's hover reveal, 66 lines in `initialize()`, `eventFilter()` and a new
method), `WaveformRendererRGB` (the stacked drawing and its state, ~190 of 240
lines).

**Sound already.** The pure rules (`syncsource`, `synctempo`, `automaster`,
`keysync`, `autoplay`, `camelot`, `folderlibrary`), `StreamingFile`,
`TrackCache`, `deckqueries`, `PdbIngest`, `DeckTrackModel`, the delegates, the
release and levels readers, and most upstream patches, each small and
commented. These stay as they are.

### Steps

Each step is one or more commits in `mixxx/` on branch `fork-refactor`; every
commit builds and passes the unit tests below. A move or a rename is a commit
of its own: the `git mv` and the include and name updates that follow from it,
and nothing else, so a reviewer can read it with `git show -M` and move on.
Edits come in the commits around it. TriMixxx gets the submodule bumps, this
document, and the docs that name moved code.

1. **The old Pro DJ Link sidebar goes.** Delete `library/prolink/`, its CMake
   block and option, `__PROLINK__` (`library.cpp`, `MediaRegistry`,
   `DlgDeveloperTools`), the `ShowProLinkLibrary` branch, the three icons and
   their `.qrc` lines, `willLoadTrack()` (`TrackModel`, `WTrackTableView`, the
   browser's call). The service's one-session guard stays: one process, one
   set of sockets is still the rule.
2. **Patches the deck never reaches go back to upstream**, one commit per
   area: the sidebar and library widgets; `TreeItem`; `LibraryControl`;
   `Library`'s default selection; `WOverview`'s RGB overview; with them a stale
   `.gitignore` line and CMake's garbled ProlinkRust comment.
3. **Dead declarations go**, by area (network values, transfers, rekordbox,
   streaming and previews, the browser, the ingest's rb_id 0 row and the two
   filters that hid it).
4. **Network values in one header.** `prolinkdefs.h` (what is left of it),
   `prolinkdevice.h` and `MediaInfo` become `prolinktypes.h`;
   `server/prolinkservestatus.h` becomes `prolinkservestatus.h`, out of the
   `server` namespace.
5. **rekordbox's readers move to `library/rekordbox/`.** `prolinkpdb` →
   `rekordboxpdb`, `prolinkanlz` → `rekordboxanlz`, from `mixxx::prolink` to
   `mixxx::rekordbox`. A move commit, then a rename commit.
6. **Two renames.** `widget/deck/deckautoplay` → `library/deck/deckautoplay`;
   `sources/soundsourceprolink` → `sources/soundsourcestreaming`
   (`SoundSourceStreaming`, `SoundSourceProviderStreaming`; the provider's
   display name in the log says "streaming").
7. **`ProLinkSync` out of `ProLinkNetworkService`.** The sync methods and
   members move verbatim, the MASTER and SYNC button handlers with them. The
   service keeps the session, hands it to `ProLinkSync` when it opens one and
   takes it back when it closes it, and calls `ProLinkSync::update()` at the
   same point of `poll()` as the three calls it replaces (`publishMaster()`,
   `publishPlayback()`, `followMaster()`, in that order, after the events and
   before the announcement and the serve status), and
   `ProLinkSync::sessionStopped()` at the same point of `shutdown()`. Then, in
   a commit of its own, the service's five transfers get one helper for their
   shared preamble (no session, the player gone, the bridge's exception), and
   `Pending` a kind instead of three booleans; every signal and error string
   stays as it is.
8. **`MediaRegistry` sheds two jobs.** `MediumId` learns to give back the
   player address it was built from, and the hand parsers use it.
   `RemoteTrackStreamer` takes `startStreaming()`, `stopStreaming()`, the
   progress and finish handlers, the companion fetch and the state they keep.
   KEY SYNC: the registry still resolves the master's key, and says it with a
   signal; it no longer owns `ProLinkKeySync`.
9. **`DeckLoader` out of `WDeckBrowser`.** `loadRow()`, `readLibraryRow()`,
   pinning and release, the cover and its guard, the BPM write-back, the play
   log, the loaded row. The browser reads its own model's row and hands it
   over; autoplay calls `DeckLoader::loadLibraryRow()` instead of a callback
   into the browser.
10. **`DeckServices`.** The owner of [Ownership](#ownership-and-lifetime),
    made by `CoreServices` at the end of `initialize()` (after the library,
    the decks and the skin controls; before the command-line tracks, the
    skin and the controllers), and destroyed first in `finalize()`. The
    `[ProLink]` controls move into it from `CoreServices`: still before any
    skin. It takes the tables' setup and the boot purge out of the browser's
    constructor, in the same order. `MediaRegistry::instance()`,
    `whenReady()`, `TrackCache::instance()`, `DeckAutoplay::instance()`,
    `whenReady()` and `ProLinkControls::instance()` go: each user is handed
    what it uses. The deck group is named once, in `DeckServices`.
11. **Fork code out of upstream files, behind small hooks.**
    `parseDeckNode()` (one branch in `parseNode()`), `MenuBarReveal` (one line
    in `MixxxMainWindow::initialize()` instead of three hunks),
    `StackedWaveform` (one call in `WaveformRendererRGB::draw()`).
12. **The docs follow**: this map, without this section; `docs/tempo-sync.md`,
    `docs/browser-streaming.md`, `docs/browser-preview-waveform.md`,
    `docs/browser-status.md` and `docs/plain-usb-plan.md` where they name code
    that moved; the root `CLAUDE.md` table points here.

Expected size: the fork's own code about 2.5k lines shorter; upstream files
touched from 57 to about 47, their added lines from ~1.6k to ~1.0k.

### Proving nothing changes

- **Unit tests, the same ones, before and after.** One filter, run at
  `792027c` and at the end: every suite in the fork's test files
  (`Autoplay*`, `DeckLevels*`, `DeckRelease*`, `WDeckDiagnostics*`,
  `Folder*`, `KeySync*`, `PreviewWaveform*`, `ProLinkKeySync*`,
  `PresentRanges*`, `Streaming*`, `SyncSource*`, `MeterDeck*`, `SyncTempo*`,
  `TrackCache*`, `AudibleBeatClock*`, `AutoMaster*`) and the upstream suites
  the patches touch (`EngineBuffer*`, `PlayerManager*`, `TrackUpdate*`,
  `AutoDJProcessor*`, `SoundSourceProxy*`, `EngineSync*`). Each step runs
  the suites it touches. A test changes only where a signature does (a
  constructor handed what it used to look up), and only `availableFrom()`'s
  assertions go, with it.
- **Emulated decks, one scenario, before and after**, from a script kept in
  `.crew/evidence/`, so the two runs are the same keystrokes: boot to the deck
  view; every browser level; a rekordbox stick and a folder stick inserted and
  browsed; a track loaded and played, with its waveform, tempo, cues and a
  loop; autoplay across a track change; Diagnostics; two decks linked
  (`deck up --link`) with SYNC, MASTER and the phase meter; a stick pulled
  mid-track. Screenshots of still screens are compared pixel for pixel (the
  clock and CPU figures masked), moving ones by eye; the logs are compared for
  the lines each step writes (loads, reads, streams, mastership). The baseline
  is the `792027c` build deployed to the same deck, not the golden image.
- **Pro DJ Link, no change on the wire.** From the code: every call into the
  Rust session after step 7 is the same call with the same arguments at the
  same point of the same poll, which the step's diff shows (the sync code moves
  verbatim and the call order is pinned above). As a check on the linked
  decks, a capture of the same scenario before and after, compared by packet
  kind and rate (`pi-qemu link capture`). A check against real CDJs is listed
  in the PR for Sam.
- **No new work on the GUI or audio threads.** Steps 7–10 move code; they add
  no timer, no poll and no query. `DeckServices` does at the end of
  `CoreServices::initialize()` what the browser did while the skin was
  parsed, a second or so earlier. The audio thread is not touched.
- **The real deck, last.** trimixxx1, lent for the final test: the result
  deployed into RAM, the scenario's single-deck half run over ssh with
  screenshots, then the deck rebooted onto its own 0.1.1, committed.

### Behaviour that does change, knowingly

None the deck shows. Two things move in time, on purpose:

- The Pro DJ Link session, the media registry and the track cache start when
  `CoreServices` finishes rather than while the skin is parsed: the same work,
  a moment earlier. On a skin with no deck widgets they now run anyway; no deck
  has such a skin. The boot purge now also runs before a track given on the
  command line loads, which is what its own comment asks for; the deck gives
  none.
- A skin reload (Preferences) keeps the session, the registry and the cache
  instead of rebuilding them. A deck never reloads its skin.

### Left alone, and why

- **Upstream's own code.** Beyond the fork's patches, untouched.
- **`PlayerManager`'s analysis rule.** ~130 lines of fork logic in an upstream
  file, but woven through the scheduler member, the deck connections and
  `PlayerManager`'s own signals; a hook would move the lines and keep the
  coupling.
- **The rest of the upstream patches.** Each is the smallest change in the
  place it has to be (an engine control, a decoder hook, a renderer option).
- **The browser's levels.** `WDeckBrowser` stays the browser: navigation,
  levels, sort and search are one job. Its menu payloads are strings
  (`"folder:"`, `"key:"`...) where a small struct would be safer; that is a
  follow-up, not a structural fault.
- **`sameServeStatus()`** stays hand-written: it compares only what changes
  the page, and `operator==` would emit on fields it deliberately ignores.
- **Small style differences** (`RamStore` as a class of statics, `SyncTempo`
  as a class with one function, `deckaccent.h` and `deckbezel.h` as two
  headers): harmonising them costs churn and buys nothing a reader feels.
- **`mixxx_config`.** Nothing there changes; `ShowProLinkLibrary 0` becomes an
  unread key, and `mixxx_config/README.md`'s "Library menu" section, which
  describes the old sidebar, is a follow-up.
- **`lib/prolink`, ttymidi, the S3, pi-qemu, the launcher.** Out of scope.

### Follow-ups found (not in this PR)

- `deck_playlists.name`, written only for the old sidebar, is `UNIQUE` on the
  playlist's path: a playlist named like a sibling fails to insert and is
  missing from the browser. Dropping the column fixes it, which is a
  behaviour change, so it waits.
- `rekordboxanalysis.cpp` (from upstream): `kColorForIDRed(0xF870900)` has one
  digit too many and reads as `0x870900`.
- `mixxx_config/README.md` describes the old sidebar ("Library menu").
- `Dockerfile`'s comment says `GTEST_FILTER` defaults to the ProLink tests;
  it defaults to `*`.
- The browser's menu payloads as a struct rather than strings.
