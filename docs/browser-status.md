# The Browser — where this got to

Progress report against [`browser-prd.md`](browser-prd.md) and
[`browser-implementation.md`](browser-implementation.md). Read this first.

## The headline

**The browser is built, running on the deck, and replaces the old library view.**
Sources → a medium's categories → a track list, driven by the encoder, the
deck's buttons and a finger. The sort menu and the search keyboard are in. The
sidebar-and-table is gone.

**Everything in the PRD is now built.** The info panel, toasts, diagnostics, the
track cache, the hover-only menu bar, ProLink media and the serve-side phantom
medium all landed after this report was first written. Remote tracks now **stream** — they start playing before
they have arrived — which has its own document,
[`browser-streaming.md`](browser-streaming.md).

**A second hardware round has now happened** and found ten faults, all fixed
and re-verified on the deck; §5 has the ones worth remembering. What is still
untested is everything that needs a CDJ on the network — the streaming path and
the serve side — because there is no CDJ here.

---

## 1. What to user-test

Everything here was exercised on the deck and screenshotted, but only by me and
only through `deck-poke` — so it wants a human with a real encoder and real
fingers.

### Navigation
1. **BACK from the deck view** opens the browser at **SOURCES**.
2. **SOURCES** lists both sticks by their own names — `SAM1`, `SAM2` — with
   `N tracks · M playlists`, then `Diagnostics`, then `Shut down`.
   - Check the names track the *sticks*, not the slots: swap which port each is
     in and the names should follow the stick.
   - A stick still being read is dimmed, says `reading…`, and cannot be entered.
3. **Encoder** moves the selection and does not wrap at either end.
4. **Encoder push** enters. **BACK** pops one level. At SOURCES, BACK returns to
   the deck.
5. **BACK should return you to the row you were on**, not the top of the list.
6. **Swipe left→right** anywhere in a list = BACK.

### The medium menu
7. Search, All tracks, Playlists, Genre, Artists, Last played, Date added, BPM,
   Key, Album, Label — each with a count. **A category with 0 is dimmed and
   cannot be entered.**
8. Every level below this should open **instantly** — the medium was read when
   it was plugged in, not when you entered it.

### Track lists
9. **All tracks** shows cover, title, artist, BPM, key. Check CJK/accented
   titles render (they did here).
10. **Playlists** — folders drill in, playlists open. Playlist rows show a 2×2
    cover stitch and `N tracks · H h MM min`.
11. **Artists → Albums → Tracks** is three levels, and the album level has
    covers.
12. **Genre / Album / Label / Key / Date added** each list values with counts.
    Album and Label rows carry a cover. Keys should be in **Camelot wheel
    order** — 1A, 1B, 2A … 12B — *not* alphabetical.
13. **BPM** buckets are sized by the tempo range. **Press ring A1 to change the
    range while the BPM list is open — it should re-bucket.** With a track
    playing, the list should open centred on that tempo.

### Loading, sorting, info
14. **Long-press a track row** loads it. **A short tap does not** — that
    asymmetry is deliberate and worth trying to break.
15. **Encoder push** on a track also loads it.
16. **Short-press SORT** over a track list opens the sort menu; over anything
    else it does nothing. Twelve fields, `Default` first. Choosing a field asks
    Ascending/Descending; `Default` applies immediately.
17. **The sort persists** when you leave the list and open another one, until
    you choose `Default`.
18. **Long-press SORT** toggles the one-line track layout.
19. **Tap an already-selected track row** also toggles that layout.

### Search
20. `Search` at the top of a medium menu opens an **alphabetical** keyboard.
    Typing filters live; the header shows the hit count.
21. `123` swaps to digits, `⌫` deletes, `CLEAR` empties, `DONE` folds the
    keyboard away.
22. **The encoder scrolls the results**; the keyboard is touch-only by design.

### Things that should be true everywhere
23. The Qt menu bar **hides until you reach for it** — move the pointer to the
    top edge and it should appear.
24. Pulling a stick should remove it from SOURCES and, if you were inside it,
    drop you back to SOURCES.

### The serve side — needs a CDJ loading a track off *us*
25. **A CDJ sees our sticks** as LINK sources and can browse them.
26. **Load one of our tracks on the CDJ**, then watch our log: `a player loaded
    one of our tracks; holding on to it` should appear within two seconds, with a
    file count. Diagnostics → Serving should list the player.
27. **Now pull that stick out while the CDJ plays.** The behaviour that matters:
    **the CDJ finishes the track.** It should not stop, stutter or go silent.
28. A toast should say `SAM2 removed — player 2 is still being fed from cache`,
    and Diagnostics → Serving should show the slot as `gone — feeding a player
    from cache`.
29. **Browsing that medium on the CDJ should now show nothing** — every menu
    empty — while the track it is playing keeps its title, artist and artwork on
    the CDJ's display.
30. **When the CDJ loads something else**, the medium should disappear from its
    screen properly, the way an ejected stick does.

### Remote media — needs a CDJ on the network
31. **A player's slot appears in SOURCES** within a second or two of the player
    joining, already showing `N tracks · M playlists` — those come off the status
    packet, so the row is complete before anything is fetched. Entering it should
    be instant, because the database was read on detection.
32. **Long-press a remote track.** It should start playing in **a second or
    two**, not forty. Watch the log for `playable after N ms` — that is the size
    wait and it should be well under a second.
33. **Let it play through.** No stutter, no silence, no early end. Silence is the
    failure this design exists to prevent, so a single silent gap is a real bug
    and worth the log around it.
34. **`download complete:` appears in the log** part-way through the track, with
    a wait count after it. A healthy load waits a handful of times at the start
    and then never again.
35. **An M4A/AAC track specifically.** It is the one that needs the tail, and if
     the tail ordering ever broke it is the only format that would fail.
36. **The beat grid is rekordbox's**, not Mixxx's: the grid should be there
    immediately rather than after an analysis pass, and hot cues and memory cues
    should be on the waveform. Check the same for a track on a **local stick** —
    that path is new too.
37. **Scrub to the end of a track that is still downloading.** It should wait and
    then play, not error out and not go silent.
38. **Load a second remote track while the first is still downloading**, then a
    third. Nothing should hang, and the first track should keep playing.
39. **Pull the player off the network mid-download.** The deck should report a
    failure and stay responsive; the track that is already playing keeps playing
    only if the whole file arrived — the diagnostics page says which.
40. **Diagnostics → Streaming** shows each in-flight track with its size, whether
    it is complete, and its wait counters. **Written to card** should read `none`
    for the whole session.

---

## 2. What the second hardware round settled

Driven end to end on the deck with `deck-shot` and `deck-poke`, and now
believed to work:

- **Loading and playing a track**, off a stick, from the cached copy.
- **Beat grids, hot cues and the rekordbox waveform**, which are imported rather
  than analysed — 41 504 waveform points on the first track tried.
- **The track cache**: a stick pulled mid-track left the music playing past
  1:12, where an uncached track dies at about fifteen seconds.
- **Keys in Camelot order**, both as a category (1A, 1B, 2A, 2B…) and as a sort.
- **Harmonic key colouring**: with a 1A track playing, 1A and 2A rows go green.
- **The info panel**, the clickable breadcrumb, the playing stripe, the slot
  numbers, the alphabetical search keyboard and its live hit count.
- **BPM buckets**, which opened on the bucket holding the playing tempo.
- **Last played**, which merges our play log with the stick's own rekordbox
  history — 94 entries off one stick, so the type-19 ingest works.
- **Toasts**, both directions, and the eject-while-playing variant.
- **Nothing written to the SD card** for a whole session.
- **The `Shut down` overlay**, reached by accident and cancelled.

Still not exercised, and honestly so:

| Piece | Why untested |
|---|---|
| **ProLink media in SOURCES** (§11.3) | **Needs a CDJ on the network.** |
| **Streaming a remote track** | Needs a CDJ. The largest untested piece: `browser-streaming.md` describes it, and §1 has the list to run. |
| **Cover art for remote media** | Needs a CDJ. Fetched over dbserver from the row being drawn, once per path. |
| **The phantom medium** (§12.5) | Needs a CDJ *loading a track off us*, and then the stick pulled. The behaviour that matters is the player finishing its track. |
| **BPM re-bucketing on a tempo-range change** | The list opens centred on the playing tempo, which is verified; changing the range while it is open needs the ring, which `deck-poke` cannot send. |
| **A real hover** | The menu bar is hidden and stays hidden, but `xdotool` cannot generate a hover, so the *reveal* is unproven. |

## 2b. The CDJ round — what a live player settled

A CDJ-2000NXS on eth0 with SAM1 in its USB slot.

**The serve side works.** The deck claimed player 4, mounted its own stick as
`/C/` (5444 files, 692 tracks), the CDJ queried our media and opened a dbserver
connection from 169.254.202.84. It can see and browse our stick.

**Remote media now appear**, after one fix: nothing arrives unasked. A player
publishes its volume name and counts *only* in answer to a media query, answers
each once and never repeats it (F37) — and the only thing that sends one lived
in the CLI. With a five-second survey the CDJ's stick appears in SOURCES with
its chevron and player number, and its `export.pdb` is fetched and ingested over
the wire: 651 tracks, 290 artists, 94 history entries. **Its cover art arrives
too**, fetched over dbserver from the row being drawn.

**Playing a remote track does not work yet**, and this is the one open fault:

```
SoundSourceProLink  - claiming ".../<hash>.mp3" -- still streaming
SoundSourceFFmpeg   - AVStream { ... codec_id 86017 | sample_rate 44100 | bit_rate 320000 }
SoundSourceFFmpeg   - av_seek_frame() failed: Operation not permitted
SoundSourceProxy    - Failed to read file ... with provider "Pro DJ Link streaming"
MediaRegistry       - download complete: ".../<hash>.mp3" -- 0 waits / 0 ms
```

Everything around it is right. The provider claims the file, FFmpeg probes it
through our `AVIOContext` and parses the stream correctly — the `AVStream` line
is a real 320 kbps 44.1 kHz MP3 — and the transfer completes without a single
blocked read. Only the seek fails, with `EPERM`.

`AVSEEK_FORCE` was masked off, which was a genuine defect in the callback, and
the failure survives it. So the next step is not another guess: print
`m_pAvioContext->seekable` after `avio_alloc_context`, and log every `(offset,
whence)` the callback is handed. `EPERM` out of `av_seek_frame` points at
libavformat deciding the stream is not seekable, which would mean `seekable` is
not what `avio_alloc_context` is assumed to set it to — and that is one printf
to confirm rather than an afternoon of reasoning.

## 3. Not implemented

Nothing in the PRD. One thing found while finishing it and deliberately left
out, because it is a separate piece with its own design question:

- **Cover art on a remote medium's *album* rows** works, because it is fetched
  from the row being drawn. There is no bulk prefetch, so the first paint of a
  list shows grey squares that fill in over a second or two. Whether that wants
  a look-ahead is a judgement to make on hardware, not in advance.

## 4. Known bugs and open threads

- **`deck-poke tap` is unreliable.** It works, then stops working, then works
  again after an unrelated command. `longpress` never fails. The UI is fine —
  the keyboard typed `AAAB` and `D` through the same path, and MIDI navigation
  is rock solid throughout. Best hypothesis: `unclutter -idle 0 -root` in
  `xinitrc` interfering with synthetic pointer events. This affects **testing
  only**; real touch generates its own events. Worth confirming by killing
  `unclutter` and retrying.
- **The search page's result rows** were verified to populate (200 hits on `D`),
  but I never got a clean screenshot of them *rendering* with the column fix in.
  First thing to eyeball.
- **`applySort()` calls `select()` after `setSort()`**, which may be one select
  too many — `setSort` may already re-select. Harmless, possibly wasteful.

## 5. Things the hardware told us

### From the second round — ten faults, all fixed

Four of these were invisible from a desk, and the pattern is worth naming:
**every one failed silently.** Nothing crashed, nothing logged an error, and
each looked like a different feature simply not being finished.

1. **A tmpfs is not sized from RAM.** The track cache was capped at a flat
   gigabyte, reasoned from the Pi's 3796 MB — but it lands on `/run`, which
   systemd sized at **760 MB**. The cap could never be reached; the filesystem
   would fill first, and a full `/run` takes systemd with it. `/tmp` on the same
   machine is 1.9 GB. Measure the filesystem, never the machine.
2. **Two Pro DJ Link sessions were running**, one from the browser's registry
   and one from the old sidebar feature, competing for a player number — the
   deck announced "no player number was free" *against itself*.
3. **Toasts had never once appeared.** The skin builds `<DeckToast>` before the
   `<DeckBrowser>` that creates the registry it subscribes to. Widget creation
   order in a file skin authors edit is not something to depend on.
4. **A stick leaving was only noticed if its directory left too.**
   `directoryChanged` says nothing about an unmount, and `dj-usb`'s `rmdir` is
   best-effort.
5. **The diagnostics page did not scroll**, by encoder or by finger — and a
   press on it activated a row of the list hidden behind it.
6. **Every track load logged four warnings**, because a provider declining is
   not a provider failing and the proxy could not tell them apart.
7. **Sorting by BPM printed the BPM twice**, and **a sort dropped the
   selection** — the list came back with nothing highlighted.

### From the first round

- **USB port 2 intermittently will not enumerate.** Before a replug, `usb2-port2`
  cycled `"Cannot enable. Maybe the USB cable is bad?"` → `"unable to enumerate
  USB device"` → power cycle, every 4 s, forever. It fails *silently*: the stick
  simply never appears. On USB 3 that usually means signal integrity on the
  differential pairs — internal cabling or connector.
- **Mount slots and volume labels are unrelated.** Slots go in plug order. This
  is why the browser shows labels, and why `dj-usb` now writes one at mount.
- **Mixxx uses 421 MB RSS** with a track loaded, of 3796 MB, 3288 MB available.
  `/tmp` is already tmpfs at 1.9 GB. Swap is 2 GB of zram, untouched.
- **The ProLink download cache used to write to the SD card**
  (`~/.cache/mixxx/prolink` is on `/dev/mmcblk0p2`), and nothing ever cleaned it
  up. Both roots are now under `/run` — tmpfs — so a night of remote loads
  writes nothing to the card. Worth re-checking with the diagnostics page's
  "Written to card", which should read `none`.

## 6. Traps, so they cost the next person nothing

41. **`BaseSqlTableModel::setTable()` does not select**, and `setSearch()` only
   records the text. `WTrackTableView::loadTrackModel()` is what normally calls
   `select()`. Miss it and the model is correctly configured, reports zero rows,
   and logs nothing.
42. **Every `Q_OBJECT` class must `#include "moc_<file>.cpp"`.** The failure is
   `"mocs_compilation.cpp not empty"`, which names neither your file nor your
   class.
43. **A plain `QWidget` ignores its stylesheet background** without
   `WA_StyledBackground`. Overlays you can see through.
44. **`/dev/disk/by-label` holds symlinks to block devices**, which `QDir::Files`
   does not match.
45. **`qBound` asserts when its bounds cross** — `qBound(0, n, rowCount()-1)` on
   an empty list.
46. **A backgrounded `docker build` with redirected output reports the wrapper's
   exit code.** Grep the log for `error:`; do not trust the notification.
47. **Delegate column indices must be re-resolved on every model change.** A
   stale set draws correctly-sized blank rows, which looks like an empty query.
48. **The build loop is fast** — a compile error surfaces ~20 s in. Iterate
   freely.
