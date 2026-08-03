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

Nothing added since the first hardware round has been run on hardware. The
streaming path in particular has never seen a CDJ.

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

## 2. Implemented but never exercised

Compiles and ships; I could not drive it.

| Piece | Why untested |
|---|---|
| **Last played** | Needs play history. `deck_play_log` is never written yet, and the stick's own rekordbox history needs a `prolink-cxx` bridge change that is not made — so this list is empty by construction. |
| **Harmonic key colouring** | Implemented (exact match + relative + ±1 on the Camelot wheel) but needs a track *playing* to have a reference. Nothing was loaded during testing, so no key ever went green. |
| **`Shut down` root row** | Wired to the existing confirmation overlay; never clicked. |
| **Medium eject while browsing it** | The unwind-to-SOURCES path is written and never triggered. |
| **A second read of a changed stick** | Re-ingest replaces by `UNIQUE(medium, rb_id)`; only ever seen on first insert. |
| **BPM re-bucketing on tempo-range change** | The query re-runs on `rateRange`; never poked A1 while the list was open. |
| **`—` rows** for empty values, and the empty-root state | Both are code paths no stick here triggered. |

Since that round, a second body of work landed and **none of it has been run on
hardware either** — the deck was unreachable, then had no CDJ on the network.
All of it compiles for arm64 and its unit tests pass:

| Piece | Why untested |
|---|---|
| **Track cache** (§12) | Needs a stick pulled mid-track. |
| **Diagnostics page** (§14) | Needs the deck. |
| **Toasts** (§13) | Needs a stick inserted and ejected. |
| **Hover-only menu bar** (§4.4) | Needs the deck. |
| **ProLink media in SOURCES** (§11.3) | **Needs a CDJ on the network.** |
| **Streaming a remote track** | Needs a CDJ. The largest untested piece: `browser-streaming.md` describes it, and §1 has the list to run. |
| **Cover art for remote media** | Needs a CDJ. Fetched over dbserver from the row being drawn, once per path. |
| **The phantom medium** (§12.5) | Needs a CDJ *loading a track off us*, and then the stick pulled. The behaviour that matters is the player finishing its track. |
| **Beat grids from ANLZ** | Applies to local sticks too, and was never checked there — a loaded track should arrive with rekordbox's grid and cues rather than being analysed from scratch. |

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
