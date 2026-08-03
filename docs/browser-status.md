# The Browser — where this got to

Progress report against [`browser-prd.md`](browser-prd.md) and
[`browser-implementation.md`](browser-implementation.md). Read this first.

## The headline

**The browser is built, running on the deck, and replaces the old library view.**
Sources → a medium's categories → a track list, driven by the encoder, the
deck's buttons and a finger. The sort menu and the search keyboard are in. The
sidebar-and-table is gone.

**It is not the whole PRD.** The info panel, toasts, diagnostics, the track
cache, the hover-only menu bar and everything ProLink are not built. Details
below.

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
23. The Qt menu bar is **still visible** — the hover-only behaviour is not built.
24. Pulling a stick should remove it from SOURCES and, if you were inside it,
    drop you back to SOURCES.

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

Nothing ProLink-specific was written, so there is **no blind remote work** to
test. Remote media still run the old code path and will not appear in the new
SOURCES list.

## 3. Not implemented

- **Info panel** (§8.2) — the layout toggles to one line per row, but the
  right-hand panel of fields does not exist.
- **Toasts** (§13) — no insert/eject notifications at all.
- **Diagnostics** (§14) — the root row exists and does nothing.
- **Track cache** (§12) — the deck still plays straight off the stick, so a
  pull mid-track will still stop it after ~15 seconds. **This is the one
  omission with a live failure mode.**
- **Hover-only menu bar** (§4.4).
- **Serve-side phantom medium** (§12.5).
- **ProLink media in the browser** (§11.3) — remote sources do not appear.

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
- **The ProLink download cache writes to the SD card** (`~/.cache/mixxx/prolink`
  is on `/dev/mmcblk0p2`). Every remote track fetched today is a card write.

## 6. Traps, so they cost the next person nothing

1. **`BaseSqlTableModel::setTable()` does not select**, and `setSearch()` only
   records the text. `WTrackTableView::loadTrackModel()` is what normally calls
   `select()`. Miss it and the model is correctly configured, reports zero rows,
   and logs nothing.
2. **Every `Q_OBJECT` class must `#include "moc_<file>.cpp"`.** The failure is
   `"mocs_compilation.cpp not empty"`, which names neither your file nor your
   class.
3. **A plain `QWidget` ignores its stylesheet background** without
   `WA_StyledBackground`. Overlays you can see through.
4. **`/dev/disk/by-label` holds symlinks to block devices**, which `QDir::Files`
   does not match.
5. **`qBound` asserts when its bounds cross** — `qBound(0, n, rowCount()-1)` on
   an empty list.
6. **A backgrounded `docker build` with redirected output reports the wrapper's
   exit code.** Grep the log for `error:`; do not trust the notification.
7. **Delegate column indices must be re-resolved on every model change.** A
   stale set draws correctly-sized blank rows, which looks like an empty query.
8. **The build loop is fast** — a compile error surfaces ~20 s in. Iterate
   freely.
