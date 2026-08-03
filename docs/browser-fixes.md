# The Browser — fixing what the first run found

Against [`browser-status.md`](browser-status.md), from testing on the deck. Nine
items: four are bugs with a diagnosed root cause, three are missing behaviour,
two are new requirements.

Ordered by what hurts most.

---

## 1. Reading a stick takes 15 seconds and freezes the GUI

**Two causes, one fix for both.**

### The ingest never opens a transaction

`pdbingest.cpp` contains no `ScopedTransaction` — grep says zero. So SQLite
runs **every INSERT as its own transaction**: 651 tracks, plus a playlist row
each, plus one entry per playlist membership, is well over a thousand
autocommits. Each one is a journal write and an fsync to the SD card, at
roughly 10–20 ms. That is the 15 seconds, almost exactly.

Upstream's Rekordbox feature wrapped its writes in a `ScopedTransaction`; my
rewrite dropped it.

### And that is also the freeze

The database is in **rollback-journal mode** — nothing in the tree sets
`journal_mode=WAL`. In that mode a writer takes an exclusive lock and **readers
block**. A thousand short write transactions on a pooled thread means the GUI
thread's own queries — the counts on the medium menu, the model's `select()` —
are locked out over and over for the whole 15 seconds. The work is off the GUI
thread; the *lock contention* is not.

**Fix**

1. Wrap the whole of `writeMedium()` in one `ScopedTransaction`, committed at
   the end. This is the entire fix for the slowness: one transaction, one fsync.
2. Prepare the three statements once and rebind per row — already done for
   tracks, not for the per-playlist cover query in `playlistsIn()`.
3. Then measure again. If the GUI still stutters, enable
   `PRAGMA journal_mode=WAL` on the collection database, which lets readers
   carry on while the writer works. Worth doing on its own merits for a deck
   that reads a stick mid-set, but do it **second**, so the transaction fix is
   measured on its own.

**Verify:** time from insert to `Ready` in the log (`DeckIngest - wrote N
tracks`), expect well under a second for 651 tracks; and scroll the source list
while a second stick reads.

---

## 2. Sorting by Key produces nonsense (11A → 6A → 1A)

**Root cause.** The sort menu's `Key` field sorts on the `key_id` column, and
`key_id` is Mixxx's `ChromaticKey` enum — **chromatic order** (C, C♯, D, …), not
Camelot order. 11A → 6A → 1A is exactly what chromatic order looks like when
read as Camelot.

The *category* list is right, because `keyCategory()` sorts in C++ through
`camelotOrder()`. Only the track-model sort is wrong, because SQL has no idea
what a Camelot wheel is.

**Fix**

1. Move `camelotNumber()` / `camelotOrder()` out of `deckqueries.cpp`'s
   anonymous namespace into a shared `camelot.h`.
2. Add **`camelot_order INTEGER`** to `deck_library` and populate it at ingest
   from the same function. One integer per track, computed once.
3. Point the sort menu's `Key` field at `camelot_order`.
4. **The sorted column and the displayed column now differ for Key** — the info
   layout must show the `key` *text* beside each title, not the order number. So
   `WDeckSortMenu::Field` needs a `displayColumn` alongside `column`, defaulting
   to the same thing and differing only here.

**Verify:** sort by Key and read down the list — 1A, 1B, 2A … 12B. Cross-check
against the Key category list, which was already right.

---

## 3. The info panel never appears

**Not a bug — it was never built.** `browser-status.md` §3 lists it as absent.
Long-pressing SORT toggles the *row layout* to one line, which is only half of
§8.2; the panel on the right does not exist, so the visible effect is that the
artist disappears.

**Fix — build it.**

1. `WDeckInfoPanel`, 464 px on the right, shown only in info layout: artwork,
   then Artist, Album, Year, Duration, Genre, Key, Rating, Date added, Last
   played, Comment.
2. The list narrows to 560 px beside it.
3. **Any field that is the current sort key is omitted from the panel**, because
   it is already beside every title on the left.
4. Fields with no value are omitted entirely rather than shown blank.
5. It follows the selection: `DeckListView::selectionMoved` → repopulate. That
   signal already exists and is currently connected to an empty slot.

While in there, make the toggle unambiguous: it should be obvious at a glance
that the layout changed even before the panel is populated.

---

## 4. BPM buckets do not re-bucket live when the tempo range changes

**Root cause.** Nothing watches `rateRange`. The buckets are computed in
`showCategory()` and only recomputed when the level is rebuilt — which is why
leaving and re-entering works.

**Fix.** A `ControlProxy` on `[Channel1],rateRange` in `WDeckBrowser`; on
change, if the current level is the BPM category, rebuild it in place, keeping
the selection on the bucket containing the previously selected centre.

**Verify:** open BPM, press ring A1 through ±6 / ±10 / ±16 / WIDE, watch the
ranges widen without the menu closing.

---

## 5. Key colouring does not follow the loaded track

**Root cause.** `setPlayingKeyId()` is called once, from
`refreshTrackColumns()`, i.e. only when the model changes. Loading a track does
not touch it.

**Fix.** A `ControlProxy` on `[Channel1],key`; on change, update the delegate
and call `viewport()->update()` — no model rebuild, no menu redraw. Watch
`track_loaded` too, so unloading clears the reference and the colouring goes
away rather than lingering on the old key.

## 6. A matching key should be bold as well as green

**Fix.** In `TrackRowDelegate::paint()`, when `key::isCompatible()` is true, set
a bold font for the key text as well as the green pen. Restore the font
afterwards — the painter is shared with the rest of the row.

Worth pairing with 5: both are one-line changes in the same function, and both
are verified the same way.

---

## 7. Long-pressing BACK should leave the browser without unwinding it

**The requirement.** A short BACK pops one level. A **long** BACK returns to the
deck view *from wherever you are*, and re-opening the browser puts you back in
the same menu.

**Fix — in the mapping, not in C++.** `TriMixxx.scripts.js` already has the
short/long discrimination for SORT; `TriMixxx.back` gets the same timer.

- short → `[Browser],back` as now
- long → `[Master],show_library = 0`, and nothing else

No new control is needed, and the navigation stack survives untouched because
nothing tells it to unwind — the widget is simply hidden.

**One thing to check:** that the browser redraws correctly when it becomes
visible again. The stack is intact, but nothing currently runs on show, so if
the view needs a repaint or a re-select it will have to be added.

---

## 8. The breadcrumb should be clickable, and lead with a Home icon

**The requirement.** `⌂ › SAM2 › Artists › 999999999`, where each segment jumps
back to that level, and the root is an icon rather than the word SOURCES.

**Fix.** The breadcrumb is already a `QLabel`; make it rich text with one link
per level:

```
<a href="0">⌂</a> › <a href="1">SAM2</a> › <a href="2">Artists</a> › …
```

with `setTextFormat(Qt::RichText)`, `setOpenExternalLinks(false)`, and
`linkActivated` → pop the stack down to that index and rebuild. The href is the
level index, so no name matching is involved.

- The home glyph comes from the Nerd Font the skin already uses (``),
  which avoids shipping an icon for one character.
- Link colour has to be set in the HTML, because a stylesheet does not reach
  rich-text anchors.
- The current sort indicator stays where it is, right-aligned, and stays
  clickable — it already opens the sort menu.

**Touch target:** a link the height of a text line is small for a fingertip. The
breadcrumb bar is 48 px, so pad the anchors to fill it rather than leaving 20 px
of hit area in the middle.

## 9. Show the slot number beside the medium mark

**The requirement.** Which physical port a stick is in, so the wrong one does
not get yanked. Right now SAM2 is in slot 1 and SAM1 in slot 2 — exactly the
confusion this prevents.

**Fix.**

1. `MediumInfo` gains `int slot`, parsed from the mount point (`DJ_USB_1` → 1).
   Local media only; remote media have a player number instead, which the row
   already draws.
2. `MenuRow` gains the same, and `MenuRowDelegate` draws it immediately after
   the mark, before the name: `▊1  SAM2`.
3. Keep it visually distinct from a Pro DJ Link player number, since the two
   sit in the same place and mean different things — one is a port on this deck,
   the other is a device on the network. Dim for the slot, full brightness for
   the player number, or a different weight.

---

## Order of work

1. **The transaction** (§1) — biggest win, smallest change, and it makes
   everything else pleasanter to test.
2. **Camelot sort column** (§2) — schema change, so it wants to land with the
   ingest change rather than after it.
3. **Key colour live + bold** (§5, §6) — same function, minutes.
4. **BPM live re-bucket** (§4) — one ControlProxy.
5. **Long BACK** (§7) — script only, no rebuild.
6. **Slot number** (§9) — small, and it removes a real hazard.
7. **Breadcrumb links + Home** (§8).
8. **The info panel** (§3) — the largest piece, and the only one that is a
   feature rather than a fix.

§1 and §2 both touch the schema and the ingest, so build them together and
re-read both sticks once.

---

## Status — all nine implemented, none tested on hardware

Written blind: it compiles, it is deployed to neither deck nor eye. What to
check, in the order most likely to catch a mistake:

| # | Change | How to know it worked |
|---|---|---|
| 1 | One transaction around the ingest | A stick should read in **well under a second**, and the source list should stay scrollable while the second one reads. Watch for `DeckIngest - wrote N tracks` in `~/.mixxx/mixxx.log`. |
| 2 | `camelot_order` column, Key sorts on it | Sort by Key: 1A, 1B, 2A … 12B. **This is a schema change, so both sticks must be re-read** — they are, on every boot. |
| 3 | The info panel | Long-press SORT, or tap an already-selected track. Panel on the right, following the selection as you turn the encoder. |
| 4 | BPM re-buckets live | Open BPM, press ring A1. Ranges should widen without the menu closing. |
| 5 | Key colour follows the deck | Load a track, then browse: compatible keys go green **without** re-entering the list. |
| 6 | Matching keys bold | Same test as 5. |
| 7 | Long-press BACK | Hold BACK anywhere: straight to the waveform. Press BACK again: back in the *same* menu, not at SOURCES. |
| 8 | Clickable breadcrumb, home glyph | Tap any segment to jump back to it. The root is a house. **If the house renders as a box, the Nerd Font is not resolving that codepoint** — swap `` for a word. |
| 9 | Slot number beside the mark | `1` beside SAM2 and `2` beside SAM1, dimmer than the name. |

### Most likely to be wrong, since none of it was run

- **The breadcrumb link hit area.** Anchors are padded with `&nbsp;`, which is
  a guess at a fingertip. If segments are hard to hit, the label wants replacing
  with a widget that hit-tests properly.
- **The info panel's width against the list.** 464 + 560 = 1024 exactly, so the
  list has no margin for error; if titles look cramped, take it off the panel.
- **Last played** is now real and completely untested. The medium's half comes
  from its rekordbox history playlists (bridged through `lib/prolink`, written
  to `deck_history`); ours comes from a play logged when the loaded track passes
  half way. Check the log line `DeckIngest - wrote … N history entries`: a
  well-travelled stick should contribute far more entries than it has tracks,
  and a stick that has only ever been in a laptop will contribute **zero**,
  which is correct and looks identical to a bug.
- **The house glyph** (see 8).

### Not touched, still absent

Toasts, diagnostics, the track cache, the hover-only menu bar, and anything
ProLink. The cache remains the one omission with a live failure mode: pulling a
stick mid-track will still stop it after about fifteen seconds.

### On WAL, deliberately not done

`journal_mode=WAL` would stop readers and writers blocking each other outright,
which is exactly this deck's pattern, and it usually *reduces* card writes. It
was left out of the transaction fix on purpose, not rejected:

- It is a property of **Mixxx's whole database**, not the browser's, and it
  persists in the file header — so a regression would present as "Mixxx is
  broken" and reverting needs an explicit `journal_mode=DELETE`.
- The transaction fix already removes almost all of the contention: the writer
  now holds the lock for a fraction of a second rather than fifteen.

So: measure the transaction fix first. If inserting a stick mid-set still
hitches, add WAL as **its own one-line commit**, so whichever one helped is
attributable.
