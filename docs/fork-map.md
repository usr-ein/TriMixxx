# The fork's map

What the TriMixxx fork of Mixxx adds to upstream Mixxx 2.5.6, where each part
lives, who owns what, and where the fork reaches into upstream's own files.

Kept true by whoever changes the fork's structure next: a class that moves, a
service with a new owner, a new reach into an upstream file.

The fork is upstream 2.5.6 (`3ebac449e7e5`, "Release 2.5.6") plus the
fork's own commits. As the `fork-refactor` branch left it, the fork adds 122
files under `mixxx/src` (24.7k lines, tests included) and changes 45 upstream
files there (+1.0k / -0.1k lines); before it, 119 files (26.4k lines) and 55
upstream files (+1.7k / -0.1k).

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
| `prolinkcontrols.{h,cpp}` | `ProLinkControls`: the `[ProLink]` controls, `DeckServices`'s own. Made before any skin is parsed, because the first creator of a control key wins (see the class). |
| `prolinktypes.h` | The values that cross from the service to the rest of Mixxx: `ProLinkDevice`, `MediaInfo`, `MediaSlot`, `kThisPlayer`. |
| `prolinkbridge.h` | Between the Rust bridge's types and Mixxx's (slots, strings), for the two files that talk to the session. |
| `prolinkservestatus.h` | `ServeStatus`: what we offer the network and who is reading it. |

### `src/library/rekordbox/`: rekordbox's files

| File | What it is |
|---|---|
| `rekordboxpdb.{h,cpp}` | Reading `export.pdb` (through `lib/prolink`): tracks, playlists, history. A folder stick is described in the same types. |
| `rekordboxanlz.{h,cpp}` | Reading an analysis file, `.DAT` or `.EXT` (through `lib/prolink`): grid, cues, waveforms, previews. |
| `rekordboxanalysis.{h,cpp}` | Applying one to a `Track`: beat grid, hot cues, loops, memory cues, in frames. |
| `rekordboxwaveform.{h,cpp}` | Building Mixxx's waveforms from rekordbox's colour waveform, so a track is not decoded to be drawn. |

Beside them, upstream's `rekordboxconstants.h`, which the analysis reads.

### `src/library/deck/`: the deck's library and services, no widgets

| File | What it is |
|---|---|
| `deckservices.{h,cpp}` | `DeckServices`: the one owner of the deck's services, the network side included. Made by `CoreServices` with the `[ProLink]` controls, before anything parses a skin; started by the skin's first deck node; the skin's deck widgets are handed it. See [Ownership](#ownership-and-lifetime). |
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
| `mediumid.h` | `MediumId`: which medium, as a key that survives SQL. A remote one gives back the player and slot it was built from. |
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
 └─ DeckServices          made where CoreServices made the [ProLink] controls,
     │                    before anything parses a skin
     ├─ ProLinkControls          the [ProLink] controls; go at the end of finalize()
     │
     │   from start(), which the skin's first deck node calls; stop() at the
     │   start of finalize() takes these down in today's order, after the skin:
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
autoplay badge) had to wait for it through `whenReady()` queues, and three
classes grew an `instance()`. An owner outside the skin, which every deck
widget is handed as it is built, ends both. The `[ProLink]` controls keep
their place before any skin, as `DeckServices`'s own.

**Started by the skin, not by `CoreServices`.** On the deck's build
(`MIXXX_USE_QOPENGL`) the skin does not follow `CoreServices` at once:
`main()` sets up the controllers and enters the event loop first, and
`MixxxMainWindow::initialize()` builds the skin only once the first GL widget
is up. Services started in `CoreServices` would read sticks and poll the
network with the event loop running and no deck widget yet to hear them. So
`DeckServices` makes its controls when it is made, and everything else in
`start()`, which `parseDeckNode()` calls for the skin's first deck node (the
toast, on the TriMixxx skin). That is the skin parse in which the browser's
constructor built them until now, and a skin parse runs no event loop. So,
as today:
- every deck widget is built after `start()` and connects as it is built,
  before any read, poll or transfer can land: a boot stick whose read fails,
  or is browsed as folders, still gets its toast;
- the signals `start()` itself sends, the first rescan's among them, reach no
  widget: a stick that was in at boot still raises no toast.

A skin reload finds the services started and leaves them be.

**What stays as it is.** The `[Browser]` controls are still the browser's,
made with it. The mapping's `init()` runs on the controller thread while the
skin is parsed and can watch them before they exist; that is a bug of today's,
left alone here (see Follow-ups).

`DeckServices::instance()` exists for one caller, `parseDeckNode()`, which
starts the services and hands them to the widgets it builds. Nothing else
reaches for it.

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
| `util/cmdlineargs.{h,cpp}`, `dialog/dlgdevelopertools.cpp`, `dialog/dlgdevelopertoolsdlg.ui` | `--log-path`, and the log found there; a "ProLink only" filter. |
| `main.cpp` | Installs `PosixSignalHandler`. |
| `mixxxmainwindow.cpp` | Hands the menu bar to `MenuBarReveal`. |
| `skin/legacy/legacyskinparser.{h,cpp}` | One `parseNode()` branch and one declaration: `parseDeckNode()`. |
| `library/library.cpp`, `preferences/dialog/dlgpreflibrary.cpp`, `preferences/dialog/dlgpreflibrarydlg.ui` | Upstream's rekordbox feature is gone (the deck reads sticks itself); no save of `mixxx.cfg`. |
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
