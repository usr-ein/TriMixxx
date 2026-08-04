# The Browser — product requirements

The deck's library UI, rewritten for a standalone unit. This document is the
*what* and the *why*; the implementation plan is a separate document and comes
after this one is signed off.

Status: **revision 3 — signed off.** Every design question is answered and
recorded in §16. What remains (§17) is settled by measurement, not by decision,
and does not block implementation.

---

## 1. Why

TriMixxx is not a laptop. It has no keyboard, no mouse, no pointer precision,
and — this is the part the current UI gets wrong — **no library of its own**.
There are exactly two places a track can come from:

1. A USB stick in one of the deck's two ports, rekordbox-prepared.
2. A medium in another player's slot, shared over Pro DJ Link.

Everything else Mixxx offers in its sidebar — the local collection, Auto DJ,
crates, playlists, history, recordings, the file browser, iTunes, Traktor,
Serato, Rhythmbox, Banshee — is either unreachable on this hardware or actively
misleading, because it promises tracks the deck cannot play.

The sidebar-plus-table layout is wrong for the same reason. It is a two-axis
design for a wide screen and a mouse. This deck has one axis of input — a rotary
encoder — and four buttons' worth of intent:

| Input | Meaning |
|---|---|
| Encoder rotate | Move the selection |
| Encoder push | Go in / load |
| BACK (ring A7) | Go out one level |
| SORT (ring B6) | Short: sort menu. Long: toggle the info layout. |

Plus a 1024×600 capacitive touchscreen, which today is used for almost nothing.

So: **one column, one selection, drill in and out.** A menu stack, not a tree
plus a table. That is what a CDJ does, and it is what fits.

## 2. Scope

**In:** a new full-screen browser replacing the entire library view — sidebar,
track table, search box and all; a source-first menu hierarchy; a track list in
two layouts; a sort menu; an on-screen search keyboard; media insert/eject
toasts; a track cache that makes a mid-set stick pull survivable; a diagnostics
page; touch gestures.

**Out (this round):** the waveform, the transport, the MIDI map's *addresses*,
the ProLink protocol layer itself, and how tracks are decoded.

**Explicitly allowed:** the old library view goes away completely. No
compatibility path, no preference to bring it back. Mixxx's library *machinery*
(track models, collection, cover cache) stays — it is what loads tracks — but
none of its *widgets* survive in the skin.

**Touched anyway, because this design requires it:** the deck header loses the
POWER button (§5, §4.4); the Qt menu bar becomes hover-only (§4.4); local
rekordbox parsing moves onto the Rust parser (§11.3); tracks are played from a
local cache rather than off the stick (§12).

## 3. Vocabulary

| Term | Meaning |
|---|---|
| **Source** | A place tracks come from. Either a **local medium** (a stick in this deck) or a **remote medium** (a slot on another Pro DJ Link player). |
| **Medium** | One rekordbox-prepared volume: USB or SD, local or remote. The thing that has a name, a track count and a playlist count. |
| **Level** | One screen of the menu stack. Level 0 is the source list. |
| **Category** | Playlists / Genre / Artists / … — the ways into a medium's tracks. |
| **Track list** | A list of tracks, in one of two layouts (§8). The only level that can load anything. |
| **Browse mode** | The browser is on screen. The deck view and its header are not. |

## 4. Interaction model

### 4.1 The four hardware controls

**Encoder rotate** moves the selection by one row per detent, in whatever list
is in focus. It never wraps at the ends (a wrapping list on a 600 px screen
makes "am I at the bottom?" unanswerable). The selection stays inside the
fully-visible band — the list scrolls under it rather than the selection walking
into the bezel strip.

**Encoder push** activates the selected row:
- a source → open its category menu
- a category → open its next level, or the track list where the category has
  none
- a folder → open it
- a playlist / value → open its track list
- a track → **load it into the deck**, which switches to the deck view (the
  existing `track_loaded` → `show_library = 0` connection already does this)

**BACK** pops one level. At level 0 it leaves browse mode for the deck view.

**SORT**, short press, opens the sort menu (§9) — but **only when a track list
is on screen.** Anywhere else it does nothing at all, with no feedback beyond
the LED already being dark. SORT, long press (≥ `LONG_PRESS_MS`, 600 ms), toggles
the info layout (§8.2) and is likewise track-list-only.

### 4.2 Touch

The touchscreen is a peer of the encoder, not a fallback:

| Gesture | Effect |
|---|---|
| Tap a row — **above a track list** (source, category, playlist, folder, value) | **Go in.** A single tap, no select-first step. |
| Tap an unselected row — **in a track list** | Select it. Nothing else. |
| Tap the selected row — **in a track list** | Toggle the info layout (§8.2), with that row selected. |
| Tap the selected row — **in search results** | **Load it.** |
| **Long press** a row (≥ 600 ms) | Activate it — identical to encoder push. In a track list, that means **load**. |
| Vertical drag / flick | Kinetic scroll, when the list overflows. The row it comes to rest on in the middle becomes the selection (§4.2.1). |
| Swipe left→right (≥ 120 px, dominant axis, ≤ 500 ms) | BACK, one level. |

The asymmetry is the whole point. **Going into a menu is cheap and free to
undo** — BACK or a right-swipe, and you are out — so it costs one tap. **Loading
is neither**, so it costs a deliberate hold.

This deck has one deck: a stray tap that loads over a playing track ruins a set,
and the 56 px bezel strip is standing evidence that fingers land where they were
not invited. A 600 ms hold cannot happen by brushing the panel. Mixxx's own
`AllowTrackLoadToPlayingDeck` guard is a second net, not the first.

The single-tap rule extends to every menu that is not a track list, including
the sort menu (§9).

**Search results are the exception to the hold**, and the only one. A result is
a track somebody typed a name to find: the finding *was* the deliberate act, and
there is nothing left for a hold to confirm. The info layout is not the
alternative there that it is in a track list — the keyboard has the width the
panel would need, so the panel is hidden on that page and the toggle had nothing
to show.

#### 4.2.1 One scroll position

**The selection sits in the middle of the list and the list moves under it**,
like the reel of a slot machine. A detent moves it by a row; a flick moves it by
many. The ends are the exception and have to be: a list cannot scroll past its
own first row, so the top few and the bottom few sit where they fit and the
selection walks to meet them.

**The highlight follows the middle for the whole of a scroll**, not just at the
end of one. What is selected has to be legible at every moment of the gesture —
a reel you cannot read until it stops is a reel you have to stop to read.

**And while it is moving the highlight is an outline, not a fill.** A solid
bright-green block sliding up the screen is the one thing on that screen you
cannot read, which defeats the point of it following at all. It goes back to the
fill the moment the list stops.

**The sort menu is exempt** (§9): it is a pop-over rather than a place, so
scrolling it is scrolling to reach a row, and the selection stays put while the
list moves.

This replaces two independent positions with one. When the flick scrolled
without moving the selection, the next detent snapped the view back to wherever
the selection had been left — so the scroll a DJ had just performed was undone
by the thing they did next.

### 4.3 Focus

There is exactly one focus at a time and it belongs to the browser as a whole.
No Tab order, no sidebar-versus-table dance, no `[Library],focused_widget`
juggling in the mapping. When an overlay is up (sort menu, keyboard, shutdown
confirm) it takes all input until it closes.

### 4.4 Full screen, and the menu bar

In browse mode the deck header (artwork, title/artist, phase meter, key, time)
is hidden and the browser owns all 600 px.

**The Qt menu bar is hidden at all times, in both views, and reappears only for
a real mouse.** Show it when a pointer *hovers* — moves with no button held —
inside a 4 px strip along the top edge; hide it again when the pointer leaves
and no menu is open. Hover is the discriminator that matters: a touchscreen
cannot hover, it can only press, so this cannot be triggered by a fingertip
regardless of whether X11 is delivering real touch events or synthesised mouse
events. (Checking `QMouseEvent::source()` for synthesis is a second filter, not
the primary one — some panels present as plain pointer devices and would defeat
it.)

The bar **overlays** the top of the UI when shown; it does not push the layout
down. The skin is a fixed 1024×600 and reflowing it would clip the bottom row.

Answering the question directly: yes, this is straightforward — an event filter
on the main window plus a floating, raised menu bar widget. The only real
subtlety is the hover-versus-press distinction above.

## 5. Level 0 — the source list

The root. Every medium the deck can play from, then diagnostics, then shutdown.

```
┌────────────────────────────────────────────────────────────────────────────┐
│  SOURCES                                                          2 links  │  48
├────────────────────────────────────────────────────────────────────────────┤
│  ▊USB   SAM1                                     456 tracks · 15 playlists │  80
│  ▊USB   SAM2                                     921 tracks · 10 playlists │  80
│  ⇄▊USB  1  BOUNCY_USB                             234 tracks ·  9 playlists│  80
│  ⇄▊SD   2  SD                                      56 tracks ·  2 playlists│  80
│  ⚙      Diagnostics                                                        │  80
│  ⏻      Shut down                                                          │  80
├────────────────────────────────────────────────────────────────────────────┤
│                          (bezel dead strip)                                │  56
└────────────────────────────────────────────────────────────────────────────┘
```

**Row anatomy**, left to right:

1. **Link icon** — present only for remote media (the Pro DJ Link mark).
2. **Medium icon** — USB or SD.
3. **Player number** — remote media only, the owning player's device number
   (1–4). Local media have no number.
4. **Name** — the medium's own name. When it has none, the medium kind is the
   name: `USB`, `SD`.
5. **Counts**, right-aligned — `N tracks · M playlists`.

**Ordering:** local media first, in slot order (`DJ_USB_1`, `DJ_USB_2`), then
remote media by player number then slot (USB before SD). Diagnostics and Shut
down are always the last two rows, in that order. Order is otherwise stable — a
medium that disappears and comes back lands where it was, and nothing below it
shifts under the selection mid-turn.

**Row states:**

| State | Shown as |
|---|---|
| Reading | **Dimmed**, with whatever counts are already known. Selectable, not enterable. |
| Ready | Full brightness, name + counts |
| Unreadable | The failure in place of the counts — `no rekordbox database`, `pdb unreadable`. Not enterable. |
| Offline (remote, keep-alive lost) | Dimmed, counts kept, not enterable |

**Every medium is read as soon as it is detected**, local and remote alike — not
when it is entered. See §11. A row is dimmed for as long as its read is running,
and entering it is instantaneous once it is not.

**Shut down** raises the existing confirmation overlay unchanged (`SHUT DOWN` /
`CANCEL`, `[TriMixxx],shutdown_confirm` → `shutdown_now` → SysEx → the daemon).
The POWER button is **removed from the deck header entirely** — this row is now
the only way to it, which also gives the header its 84 px back.

**Empty root** — nothing plugged in and no players: a single centred line,
`Insert a USB stick, or link a player`, with Diagnostics and Shut down still
listed.

**Selection stability is a requirement, not a nicety.** Media appear and vanish
asynchronously. The selected *medium* stays selected across a refresh; if the
selected medium is the one that vanished, the selection goes to the row that
took its place (or to Diagnostics, if it was the last medium). The selection
never silently lands somewhere else because a stick mounted.

## 6. Level 1 — the medium menu

```
┌────────────────────────────────────────────────────────────────────────────┐
│  ▊USB  SAM1                                       456 tracks · 15 playlists│  48
├────────────────────────────────────────────────────────────────────────────┤
│  🔍  Search                                                                │  72
│      All tracks                                                       456  │  72
│      Playlists                                                         15  │  72
│      Genre                                                             22  │  72
│      Artists                                                          187  │  72
│      Last played                                                       38  │  72
│      Date added                                                            │  72
│      BPM                                                               64  │  72
│      Key                                                               24  │  72
│      Album                                                            131  │  72
│      Label                                                             48  │  72
└────────────────────────────────────────────────────────────────────────────┘
```

Eleven rows over a seven-row viewport, so this level scrolls. The counts on the
right are how many entries the category leads to, and they are worth having:
`Label 0` tells you at a glance that this stick's tags have no label data, which
is otherwise a dead end you find by entering it.

A category with **zero** entries is shown dimmed and cannot be entered.

Because the medium was read on detection (§11), opening this level and every
level below it is instantaneous. Nothing here ever waits on I/O.

## 7. Level 2 and below — the categories

### 7.1 Playlists

```
┌ SAM1 › Playlists ──────────────────────────────────────────────────────────┐
│ ┌────┐   House Mix 2026                            23 tracks · 3 h 12 min  │  88
│ │▨ ▨ │                                                                     │
│ │▨ ▨ │                                                                     │
│ └────┘                                                                     │
│ ┌────┐   Techno mixes                                        6 playlists   │  88
│ │ 📁 │                                                                     │
│ └────┘                                                                     │
```

- **Playlist row:** a 2×2 stitch of the first four tracks' artwork, the
  playlist name, and `N tracks · H h MM min` (total duration). One cover fills
  the square; two or three tile into the 2×2 with the remaining cells empty; a
  playlist with no artwork at all gets the default cover.
- **Folder row:** a folder mark instead of artwork, the folder name, and
  `N playlists` (immediate children, folders included).
- Folders drill in; the breadcrumb grows. rekordbox nests them arbitrarily
  deep and the browser follows.
- **Playlist order is the DJ's order**, as stored — never alphabetised, unless
  a sort is in force (§9.3).

### 7.2 Artists → Albums → Tracks

Three levels, as a CDJ does.

```
┌ SAM1 › Artists ────────────────────────────────────────────────────────────┐
│  Wasei 'JJ' Chidiac                                                   34   │  64
│  Wave Corners                                                         21   │  64
└────────────────────────────────────────────────────────────────────────────┘

┌ SAM1 › Artists › Wave Corners ─────────────────────────────────────────────┐
│ ┌────┐  Hot Steel EP                                            8 tracks   │  88
│ └────┘                                                                     │
│ ┌────┐  Dead Grid EP                                            6 tracks   │  88
│ └────┘                                                                     │
```

The artist list is text only — there is no artist artwork in a rekordbox export
and inventing one is worse than none. The **album** level carries covers (§7.3).

Tracks with no album collect under a final `—` row at the album level, so the
counts add up.

### 7.3 Genre, Album, Label, Key

One list of distinct values, each with the number of tracks that carry it.
Selecting one opens a track list of exactly those tracks.

**Album and Label rows carry a cover**: the artwork of the first track under
that value that has any. Which one it is does not matter — it is a visual
handle, not a claim about the release. Genre and Key rows are text only.

```
┌ SAM1 › Album ──────────────────────────────────────────────────────────────┐
│ ┌────┐  Hot Steel EP                                                  8    │  88
│ └────┘                                                                     │
│ ┌────┐  Cyberia Layer 03                                              5    │  88
│ └────┘                                                                     │

┌ SAM1 › Genre ──────────────────────────────────────────────────────────────┐
│  Acid Techno                                                          47   │  64
│  Deep House                                                          112   │  64
```

| Category | Ordering | Row |
|---|---|---|
| Genre | Alphabetical | Text, 64 px |
| Album | Alphabetical | Cover + text, 88 px |
| Label | Alphabetical | Cover + text, 88 px |
| Key | **Camelot wheel order** — 1A, 1B, 2A, 2B … 12B | Text, 64 px |

Key ordering is not alphabetical: `10A` must not land between `1A` and `11A`.
It sorts on the `key_id`-derived Camelot index, as the track table already does.

Tracks with an empty value collect into a final `—` row rather than being
dropped, so the counts across the list add up to the medium's total.

### 7.4 BPM

BPM is bucketed, and **the bucket is one tempo fader's reach**. That is the only
question a DJ asks of a BPM list: *what can I mix into this?*

**Bucket width follows the deck's current tempo range.** At ±6 %, a bucket
starting at 120 BPM is `w = 2 × 0.06 × 120 / (1 − 0.06) = 15.3` BPM wide, so its
centre sits at 127.7 and every track in it is within ±6 % of that centre. In
**WIDE** (±100 %) the half-width is capped at **±20 BPM**, so buckets are 40 BPM
wide — without the cap a single bucket would swallow the medium.

**Pressing the tempo-range button (ring A1) while the BPM list is open rebuilds
it**, keeping the selection on the bucket that contains the previously selected
centre.

**Where the list opens** — the selection lands on the bucket built around a
*reference BPM*, chosen in this order:

1. This deck's playing BPM (rate-adjusted, i.e. what you would actually be
   mixing against).
2. Failing that, a Pro DJ Link player that is playing — the tempo master first,
   then by player number.
3. Failing that, the medium's own **density peak**: build a histogram of whole
   BPMs, take the 5-BPM-wide window with the most tracks in it, and use its
   centre. This puts you where the stick's music actually lives instead of at
   `60 BPM`.

Buckets tile **outward from the reference bucket** in both directions, so the
bucket containing the playing tempo is exactly centred on it rather than
happening to contain it near an edge.

```
┌ SAM1 › BPM                       tempo range ±6 %  ─────────────────────────┐
│  112 – 127                                                            31   │  64
│  127 – 143       ← opens here, deck is playing 134.2                 128   │  64
│  143 – 161                                                            84   │  64
```

Entering a bucket opens its tracks directly — there is no exact-BPM level in
between. The whole point of the bucket is that you want all of it.

### 7.5 Date added

Grouped by the date rekordbox recorded, **newest first**, shown as stored
(`YYYY-MM-DD`). Selecting one opens that day's tracks.

```
┌ SAM1 › Date added ─────────────────────────────────────────────────────────┐
│  2026-07-28                                                           12   │
│  2026-07-14                                                            6   │
```

### 7.6 Last played

A flat track list, most recently played first, no intermediate level. It merges
two sources:

- The medium's **history playlists** — every rekordbox export carries one per
  player mount, in play order. The Rust parser already decodes them
  (`HistoryPlaylist` in `lib/prolink`); the C++ shim does not surface them yet.
  This is history from *other* decks: real CDJs write it, we mount read-only and
  never do.
- **This deck's own play log** — a table of `(medium, rekordbox track id,
  played at)`, appended when a track passes the play threshold. It lives in
  memory / a boot-scoped table and is **wiped on reboot**, like everything else
  the browser remembers (§15).

Our own plays outrank stick history at the same instant, since they are the ones
that happened tonight.

## 8. The track list

The only level that loads anything. Two layouts, toggled by long-pressing SORT
or by tapping the selected row.

### 8.1 Default layout

Columns, in order: **Cover art · Title · Artist · [sorted-by] · BPM · Key**.

```
┌ SAM1 › Playlists › House Mix 2026                             ▲ BPM  ──────┐  48
├────────────────────────────────────────────────────────────────────────────┤
│ ┌──┐ Even Mike (Dissolver Remix)      Wave Corners       144.9        3A   │  72
│ └──┘                                                                       │
│ ┌──┐ Copland OS                       WASEI "JJ" CH…     125.0        8A   │  72
│ └──┘                                                                       │
```

- No column headers. There is nothing to click them with, and 48 px is better
  spent on the breadcrumb, which also carries the current sort.
- **The sorted-by field gets a column of its own**, between the artist and the
  tempo, left-aligned because these are words and what you do with the column
  is run an eye down the starts of them. A list read *along* one field needs
  that field on every row; sorting by Album with no Album column gave no way to
  watch the albums go past. It is omitted when the field is already on the row
  — Title, Artist, BPM and Key — and for `Default`, which has no field.
- Title and artist elide right when they overflow. The artist column is fixed
  at roughly a third of what is left; BPM and Key are fixed and right-aligned.
- BPM is shown to one decimal, matching the deck's existing `BpmColumnPrecision`.
- Key is shown in the notation set in preferences (Camelot, on this deck), and
  is **coloured by harmonic compatibility** — §8.3.
- A track's rekordbox colour tag, when it has one, is a thin stripe on the row's
  left edge. Untagged tracks get no stripe.
- The playing track, if it is in this list, is marked.

### 8.2 Info layout

The screen splits. The left is a one-line list; the right is everything known
about the selected track.

```
┌ SAM1 › Genre › Techno                                         ▼ BPM  ──────┐  48
├──────────────────────────────────────────┬─────────────────────────────────┤
│  Kobra Dance                      123.0  │        ┌───────────────┐        │  64
│  Dead Grid                        133.0  │        │               │        │  64
│  MILF Stalker                     139.9  │        │    ARTWORK    │        │  64
│  Horsework                        131.9  │        │               │        │  64
│  Even Mike                        144.9  │        └───────────────┘        │  64
│  Copland OS                       125.0  │                                 │
│  ''s''peEd                            —  │  Artist     Wasei 'JJ' Ch…      │
│                                          │  Album      Dead Grid EP        │
│                                          │  Year       2019                │
│                                          │  Duration   5:42                │
│                                          │  Genre      Acid Techno         │
│                                          │  Key        11A                 │
│                                          │  Rating     ★★★★☆               │
│                                          │  Added      2026-07-14          │
│                                          │  Played     2026-07-30 23:14    │
│                                          │  Comment    ripped from vinyl   │
└──────────────────────────────────────────┴─────────────────────────────────┘
        560 px                                       464 px
```

**Left column, one line per row:** the **title**, left-aligned, and the value of
the **column currently sorted by**, right-aligned on the same line — or the
**artist** when the list is unsorted. One line, not two, so the list stays dense
enough to scan while the panel does the explaining.

**Right panel, top to bottom:** artwork, then Artist, Album, Year, Duration,
Genre, Label, **BPM**, Key, Rating, Date added, Last played, Comment.

**Any field that is the current sort key is omitted from the panel**, because it
is already on every row to the left. Sorting by BPM puts BPM beside each title
and drops it from the panel; sorting by Genre likewise; unsorted, the artist is
beside each title and the panel starts at Album.

Tempo and key are both on that list *because* of that rule, not in spite of it.
The default layout draws them on the row and this one does not, so the panel is
the only place left for them — and each drops itself when it is what the list is
sorted by. Sorting by Key leaves the BPM on the panel and sorting by BPM leaves
the key. Leaving the tempo off the list entirely meant that sorting by Key put
it nowhere at all.

The panel follows the selection as the encoder turns, with no click needed.
Fields with no value are omitted entirely rather than shown blank — a panel of
six populated rows reads better than eleven with five em-dashes.

The layout choice is sticky for the session and resets on reboot (§15).

### 8.3 Harmonic key colouring

In both layouts, a track's **key text is green when it is harmonically
compatible with what the deck is playing**, and the default text colour
otherwise. Compatible means, in Camelot terms, any of:

- the same key (`8A` against `8A`)
- the same number, other letter (`8B` against `8A`) — the relative major/minor
- the number ±1, wrapping 12→1, same letter (`7A` or `9A` against `8A`)

Nothing else. Energy jumps and ±7 tricks are a matter of taste and would turn
half the list green.

With no track loaded there is no reference and nothing is coloured. The
reference is the deck's *current* key control, so it follows keylock and pitch
rather than the file's stored key.

## 9. The sort menu

A pop-over, raised by a short press of SORT while a track list is on screen.
Thin white border, so it reads as a layer over the list rather than as something
selected in it.

```
                        ┌───────────────────────────┐
                        │  SORT BY                  │  48
                        ├───────────────────────────┤
                        │  Default                  │  56
                        │  BPM                 ▲    │  56   ← current
                        │  Key                      │  56
                        │  Title                    │  56
                        │  Artist                   │  56
                        │  Genre                    │  56
                        │  Album                    │  56
                        │  Date added               │  56
                        │  Label                    │  56
                        │  Year                     │  56
                        │  Duration                 │  56
                        │  Rating                   │  56
                        └───────────────────────────┘
```

### 9.1 Flow

1. SORT (short) opens the menu focused, with the field in force selected and
   marked with the arrow it is running in.
2. The encoder moves through the fields; push chooses one. **Touch works
   throughout**: the list flick-scrolls with the selection staying put, and a
   **single tap on a row chooses it** with no select-first step. The asymmetry
   everywhere else exists because loading a track is expensive to undo; picking
   a sort field is one tap to put back.
3. **One step.** Choosing a field applies it immediately and closes the menu,
   in the direction that field is worth reading in — ascending for text,
   descending for BPM, Date added and Rating. The list re-sorts, keeping the
   same *track* selected wherever it has moved to.
4. BACK, a right-swipe, SORT again, or **a tap anywhere outside the menu**
   closes it, changing nothing. The tap that dismisses does not also act on
   what is under it.

### 9.1.1 The direction is a toggle, not a step

**Tapping the breadcrumb's sort indicator flips it**: `▲ Album` becomes
`▼ Album`. And the selection goes to the top — asking for the other end of a
list is asking to be taken there, and staying put would leave the DJ in the
middle of a list that had visibly reversed around them. Changing the *field* is
not that, and keeps the track the DJ was looking at.

Picking the field already in force does the same flip, so the direction stays
reachable from the encoder alone with no pointer.

This replaces a second menu screen that every single sort had to walk through to
answer a question that already had a right answer. The direction now lives on
the thing that displays the direction: there is no state to remember, nothing to
confirm, and one tap to undo.

### 9.2 What "Default" means

The order the list came in: the DJ's order for a playlist, the medium's own
order for a category list. Not "sorted by position" — the deck already has
`[Library],sort_reset` for exactly this, added because there is no column id
meaning "unsorted" and no natural column to fall back on.

### 9.3 Persistence

The chosen (field, direction) is a **global browser preference**, not a property
of a list. Leaving a list and opening another applies the same sort to the new
one. It persists until the sort menu is opened and `Default` chosen, and resets
on reboot (§15).

Where a field does not exist on a medium (no labels, no dates), the list falls
back to Default *for that list only*, without clearing the preference, and the
breadcrumb shows the sort dimmed.

This replaces the current cycling-SORT-button behaviour in
`TriMixxx.scripts.js` — `sortAdvance`, `sortForget` and the LED cycle all go
away. The SORT LED becomes: dark when there is no track list, dim when a list is
shown unsorted, lit in the sort field's colour when one is in force.

## 10. Search

Reached from the `Search` row at the top of a medium menu. Scoped to **that
medium**, all of its tracks, regardless of which category the search was opened
from.

```
┌────────────────────────────────────────────────────────────────────────────┐
│  SAM1  ›  SEARCH        DEAD_                                    23 hits   │  56
├────────────────────────────────────────────────────────────────────────────┤
│  ┌──┐ Dead Grid                    Wasei 'JJ' Ch…       133.0       11A    │  72
│  ┌──┐ Deadline                     Vladimir Duby…       140.0        4A    │  72
│  ┌──┐ Dead Air (Original Mix)      Wave Corners         126.0        8A    │  72
├────────────────────────────────────────────────────────────────────────────┤
│  A   B   C   D   E   F   G   H   I                                         │  75
│  J   K   L   M   N   O   P   Q   R                                         │  75
│  S   T   U   V   W   X   Y   Z   ␣                                         │  75
│  123          ⌫          CLEAR          DONE                               │  75
└────────────────────────────────────────────────────────────────────────────┘
```

- **Alphabetical layout**, A→Z, not QWERTY. The user of this deck is finding a
  track, not typing prose, and alphabetical is faster to *scan* — which is what
  you do on a keyboard you use twice a set.
- **All caps** on the keys and in the query line. Matching is
  **case-insensitive** and diacritic-insensitive (`Björk` matches `BJORK`).
- `123` swaps the letter rows for digits and the punctuation that shows up in
  track titles: `0-9 & ' - . ( ) + #`, plus `ABC` to swap back.
- `⌫` deletes one character; press-and-hold repeats. `CLEAR` empties the query.
  `DONE` closes the keyboard and gives the whole screen to the results.
- **Results update live**, debounced ~150 ms, ranked: prefix matches on title
  first, then prefix on artist, then substring anywhere, then alphabetical.
  Capped at 200 rows.
- Fields searched: **title, artist, album**. Not genre — genre has its own
  category and including it turns one-word queries into noise.
- An empty query shows nothing and the hit count is blank — not the whole medium.
- BACK or a right-swipe: from the keyboard, closes the keyboard (same as DONE);
  from the results with the keyboard closed, leaves search.

**Search is touchscreen-only.** The encoder moves through the *results* and a
push loads the selected one, but it does not drive the keyboard. This is the one
place in the design the deck is not fully operable without the panel, and that
is an accepted trade — encoder-walking 26 keys to type `DEAD` is four times
worse than not having search at all.

## 11. Sources, in detail

### 11.1 Every medium is read on detection

Not on entry. As soon as a medium is detected — a stick mounted, a player's slot
announced — its database is fetched (remote) and parsed, in the background. The
source row is dimmed while that runs and lit when it lands, and **entering it is
then instantaneous at every level below**.

This is a change on both sides. Today a local stick is parsed lazily, on first
click into its node (`RekordboxFeature::activateChild` → `parseDeviceDB`), and a
remote medium's `export.pdb` is fetched on entry behind a modal dialog. Both
move to detection time. The level-0 counts require it anyway: there is no track
or playlist count to print for a local stick until something has read it.

**Artwork is the exception and stays on demand.** The current remote path queues
every distinct cover on the medium up front (`prefetchArtwork`); that becomes a
per-row fetch as rows come into view, with a small LRU. Covers are the bulk of
the bytes and the least urgent of them.

### 11.2 Local media

Mounted read-only by `dj-usb` at `/media/DJ_USB_1` and `/media/DJ_USB_2`, driven
by udev. Mixxx finds them by looking for `PIONEER/rekordbox/export.pdb` under
`/media`'s immediate children.

**The name shown must be the volume label** — `SAM1` — not the mount point
(`DJ_USB_1`), which is what the screen shows today. The label is not on the
mount path and is not in the pdb, so it has to be read: resolve the mount point
to its device via `/proc/self/mountinfo`, then find that device among the
`/dev/disk/by-label/*` symlinks. An unlabelled stick falls back to `USB 1` /
`USB 2` by slot.

Mount and unmount already reach Mixxx: `trimixxx-launchd` polls `/media/DJ_USB_*`
and sends `F0 7D 10 F7` / `F0 7D 11 F7`, which `PiMidiDaemon.scripts.js` turns
into a rescan. That path stays; what it pokes changes.

### 11.3 Remote media

Discovered over Pro DJ Link. Before anything is fetched, a player's status
packets already give us the medium's **name, track count and playlist count** —
so a remote row can print its full counts the moment it appears, while its
database is still being read.

The `export.pdb` fetch over NFS is a two-to-ten second operation. It runs on
detection, in the background, one medium at a time, and never blocks browsing
another. There is no modal dialog anywhere in this flow.

Loading a *track* from a remote medium still fetches its audio synchronously —
but §12 changes when that happens, and mostly it will already be cached.

### 11.4 One medium, one data model, one parser

Local and remote media must present **identical** browse capabilities — every
category, the same counts, the same sort fields. They do not today: the two
sources are parsed by two different parsers (local via the old Kaitai
`rekordbox_pdb`, remote via the Rust `lib/prolink`) into two tables
(`rekordbox_library`, `prolink_library`) with slightly different columns.

**Unifying the parse onto the Rust one is part of this project.** It is the only
way `date_added`, `label`, `play_count` and the history playlists come out of a
local stick as readily as a remote one, and four of the categories above depend
on exactly those fields.

This covers the **database**. Per-track analysis — beat grids, cues, waveforms —
is a different file format (ANLZ) read by a different parser, and both sources
already share that code, so it is not a divergence and not in scope. See §1.2 of
the implementation plan.

### 11.5 Fields that do not exist yet

| Needed for | Field | Where it is today |
|---|---|---|
| Date added category, info panel | `date_added` | Parsed by the Rust lib, dropped by both SQL writers |
| Label category | `label` | Same |
| Last played | history playlists | Parsed by the Rust lib, not surfaced through the C++ shim |
| Last played | local play log | Does not exist |
| Level 0 counts | playlist count per local medium | Countable during the parse, not currently kept |
| Playlist rows | total duration per playlist | Derivable in SQL |
| Album / Label rows | one cover per value | Derivable in SQL from `artwork_path` |
| Level 0 names | volume label | Not read (§11.2) |

## 12. The track cache, and surviving a stick pull

**The deck never plays off removable media.** Every track it plays has been
copied into a local cache first, and the deck plays the copy. Remote media
already work this way — ProLink fetches to a cache because it has no choice —
and this extends the same rule to local sticks. That single change is what makes
a mid-set yank survivable.

### 12.1 Cache-ahead

The **highlighted** track in a track list is prefetched after a ~300 ms dwell:
audio, its `.DAT`/`.EXT` analysis files, and its artwork. DJs dwell on a track
before loading it, so by the time the encoder is pushed the copy is usually
already there and the load is instant.

- One prefetch in flight at a time; USB bandwidth is the constraint.
- A load of an un-prefetched track copies it first, with a progress state, then
  loads. This is the slow path, not the normal one.
- Prefetching never touches a track the DJ has not selected. No speculative
  whole-stick copying.

### 12.2 Where the cache lives

Two tiers, RAM first, and the rule that picks between them is **whether the
bytes can be got again**.

**Tier 1 — RAM**, a `tmpfs` with an explicit size cap. Everything lands here
first. Pinned and never evicted: the track loaded in the deck, the prefetch
target, and anything a Pro DJ Link peer is currently reading from us (§12.5).

**Tier 2 — the SD card**, wiped at boot. Written **only** under RAM pressure,
and **only** for entries that cannot be re-read: a track whose stick has been
unplugged, or whose player has left the network. Everything still re-readable is
simply *dropped* when RAM gets tight — re-copying from a stick that is still in
the slot takes a second and costs the card nothing.

**So in normal operation the SD card is never written at all.** It is a spill
tier for exactly one situation: holding bytes whose only other copy has just
been pulled out of the deck.

A note on wording, because it trips people up: `tmpfs` *is* RAM. A "tmpfs tier"
and a "RAM tier" are the same tier; it is the SD tier that needs a real
boot-time wipe (a `systemd-tmpfiles` rule, or an `ExecStartPre` on the session).

**Where it lives today is wrong on both counts.** The ProLink download cache
uses `QStandardPaths::CacheLocation`, which on the deck resolves to
`~/.cache/mixxx/prolink` — on `/dev/mmcblk0p2`, the SD card. Every remote track
fetched today is a card write. That moves to tier 1.

**Watch the zram interaction.** The deck's swap is 2 GB of zram
(`/dev/zram0`, priority 100, currently 0 used) under `rpi-swap`, which is
zram *plus a writeback file*. tmpfs pages are swappable, and compressed audio is
incompressible, so pushing tier 1 hard enough to swap gains nothing and can end
up writing to the card anyway — the exact outcome this design exists to avoid.
The defence is to keep tier 1 comfortably inside free RAM rather than to rely on
swap, which the budget below does.

### 12.3 The budget

Measured on the deck, running, with a track loaded:

| | |
|---|---|
| Total RAM | 3796 MB (Pi 4B, 4 GB) |
| In use, whole system | 508 MB |
| **Available** | **3288 MB** |
| Mixxx RSS | 421 MB (284 MB anon) — with `--developer --log-level debug` |
| Xorg | 91 MB |
| `/tmp` | already `tmpfs`, 1.9 GB |
| Swap | 2 GB zram, 0 used |
| Root filesystem | 58 GB card, 51 GB free |

From the source, and it is the number that makes this whole section necessary:
**Mixxx holds 5 MB of decoded audio per deck** — 80 chunks × 8192 frames × 2 ch
× 4 B (`cachingreader.cpp`). At 44.1 kHz that is **14.9 seconds in total**,
played and unplayed together, so the readahead alone is less. *That* is how long
the deck survives a stick pull today: under fifteen seconds. A 6-minute waveform
is a further ~1.3 MB. Track data is not where Mixxx's memory goes; Qt, the GL
scene and the skin are.

So: **tier 1 = 1 GB**, leaving ~2.2 GB of headroom over everything currently
running — enough to stay clear of zram, and enough to survive the
whole-filesystem-on-tmpfs plan later. For scale, 1 GB is roughly 70 MP3s at
320 kbps but only 16 six-minute WAVs, and WAV/AIFF is what will actually size
this at ~63 MB per six minutes. Tier 2 is capped at 4 GB against 51 GB free.

### 12.4 Ejecting a stick while playing from it

When a medium goes away and the loaded track came from it, the toast changes:

> `▊USB SAM2 removed — current track is cached and stays playable.`

and playback continues, untouched, because it was never reading from the stick.

If the track somehow is *not* fully cached — it should not happen, but a load
interrupted mid-copy could do it — the toast says so instead, because a DJ who
is about to lose audio deserves to know before it happens rather than after:

> `▊USB SAM2 removed — current track is NOT cached and may stop.`

Either way the medium disappears from level 0, and if the browser was inside it,
it pops back to level 0. Its cached files are not evicted while pinned.

### 12.5 The serve side

**A real CDJ does not buffer the whole track.** It holds an emergency loop of
what is playing right now and streams the rest. So a stick pulled while a CDJ is
playing from us is a CDJ that stops several seconds later, mid-set, with no
warning. Preventing that is the requirement.

**Cache the whole file the moment a peer starts reading it** — not as it is
served. Caching-as-served keeps only the bytes already delivered, which is
precisely the part the player no longer needs. A read of a track's first block
is the signal that a player has loaded it; at that point copy the *entire* file,
plus its analysis files and artwork, into the cache. It is our own stick and the
copy is cheap.

**When the stick goes while a peer is consuming it**, the medium does not
disappear — it goes **phantom**:

- Still **announced as present**, because that is what keeps the peer's mount
  valid and its player from throwing an error.
- **Not browsable**: queries that would open something new report it as empty,
  so no DJ can start a track we cannot finish.
- Reads and metadata for tracks already in the cache are answered exactly as
  before. The consuming player finishes its track and never knows.

**Then it leaves cleanly.** Every player's status packets carry what it has
loaded — device number, slot, track id — which is how we know a consumer is
still ours. When every consumer has moved to another medium, announce the
unmount so the medium disappears from their screens properly instead of
vanishing under a playing track.

This is a **first cut on purpose.** The behaviour that matters — a CDJ finishing
its track after the stick is gone — is testable end to end, and the protocol
details of how best to hide a medium while keeping its mount alive are worth
settling against a capture rather than a guess (§17.2).

### 12.6 What this does not do

It does not make *browsing* survive. A pulled stick's tracks stop being listable
immediately and its rows go. Only the loaded track, and whatever a peer has
already taken, survive.

## 13. Toasts

A media event raises a toast in the **top right**, over whatever is on screen —
browse mode *and* the deck view, because a stick landing while you are mixing is
exactly when you need to know.

```
                                              ┌──────────────────────────────┐
                                              │ ▊USB  SAM4 inserted          │  72
                                              └──────────────────────────────┘
```

| Event | Text |
|---|---|
| Local medium mounted | `<icon> SAM4 inserted` |
| Local medium unmounted | `<icon> SAM4 ejected` |
| Remote medium appeared | `<link><icon> BOUNCY_USB inserted` |
| Remote medium gone | `<link><icon> BOUNCY_USB ejected` |
| Unmounted while playing from it | `<icon> SAM2 removed — current track is cached and stays playable.` |
| …and not cached | `<icon> SAM2 removed — current track is NOT cached and may stop.` |
| Unmounted while a peer is consuming it | `<link><icon> SAM2 removed — player 2 is still being fed from cache.` |
| Medium unreadable | `<icon> SAM4 — no rekordbox database` (once, on insert) |

- 360 × 72 px, 16 px from the top and right edges. The two eject-while-playing
  variants are wider and two lines, since they say something that matters.
- 3.2 s, then a 200 ms fade. Up to three stack downward, newest at the top;
  a fourth event retires the oldest immediately.
- Never modal, never focusable, never stealing input. Touching one dismisses it.
- A player appearing or disappearing *without media* is not a toast — that is
  diagnostics-page material. Only media the deck could play from.

## 14. Diagnostics

Second-to-last row of the root menu, no sub-levels: one long scrolling page,
encoder and touch both scroll it. Read-only — nothing on this page changes deck
state.

The brief is "all the shit I need to fix TriMixxx", to be rearranged later, so
this is a first cut of sections in a sensible order:

1. **Identity** — Mixxx version and fork commit, skin, boot mode (mixxx/doom),
   uptime, current time.
2. **Audio** — device, sample rate, buffer frames and the resulting latency,
   `audio_latency_overload_count`, underruns split by cause (only code 6 means
   the DAC), current engine CPU load.
3. **MIDI link** — is the ttymidi port there, is the S3 answering, time since
   the last message in each direction, ring A/B node counts as enumerated,
   messages/s.
4. **Pro DJ Link** — listening or not and on what interface; our device number
   and name as announced; a table of players (number, model, IP, MAC, last
   seen, tempo master); per-slot media with name and counts; the serve side —
   what we expose, who has consumed it, request and error counts. Much of this
   is already rendered by `ProLinkFeature::statusHtml()` and folds in as-is.
5. **Media and cache** — each mount: device, filesystem, size and free, label,
   parse result and how long it took, track/playlist counts. Then the track
   cache: **tier 1 and tier 2 sizes against their caps**, entry counts, what is
   pinned and why, hit rate, evictions, and **bytes written to the SD card this
   session** — which should read zero on a normal night, and is the one number
   that says whether §12.2 is holding.
6. **System** — CPU per-core with a 60 s sparkline, load average, SoC
   temperature with a sparkline, **throttling flags** (`vcgencmd get_throttled`
   — under-voltage on a Pi is the single most common cause of weirdness and
   deserves a red line when it fires), memory used/free/cached with a sparkline,
   **swap in use** (2 GB of zram — anything above zero means tier 1 is too big,
   §12.2), root filesystem usage, network interfaces with addresses and link
   state.
7. **Recent events** — a ring buffer of the last ~200: mounts, ejects, ProLink
   device up/down, track loads, cache misses, parse failures, xrun bursts, mode
   switches, with timestamps.

Rendered as HTML in a text browser — the same widget the ProLink status page
already uses — so the layout stays editable without touching layout code. Charts
are painted to images and embedded as document resources; there is no JavaScript
in Qt's text browser, so a sparkline cannot be drawn client-side.

Refresh every 1 s while the page is on screen and never while it is not.

## 15. What persists

**Nothing across a reboot.** Every boot starts identical: level 0, no sort
(Default), default layout, empty play log, empty track cache.

Within a session:

| State | Lifetime |
|---|---|
| Sort field + direction | Until changed to Default, or reboot |
| Default vs info layout | Until toggled, or reboot |
| Local play log (§7.6) | Session; wiped at boot |
| Track cache (§12) | Session. Tier 1 is tmpfs and evaporates; tier 2 is wiped at boot. LRU-capped within the session. |
| Browser position (level, selection) | Not restored — the browser always opens at level 0 |

## 16. Decisions taken

| # | Question | Answer |
|---|---|---|
| 1 | `All tracks` in the medium menu | Yes, first after Search |
| 2 | Artist hierarchy | Artists → Albums → Tracks |
| 3 | BPM granularity | Buckets sized by the current tempo range; ±20 BPM cap in WIDE; rebuild when the range changes |
| 4 | Date added granularity | Day, newest first |
| 5 | Last played | Stick history + local play log, merged; log wiped on reboot |
| 6 | Persistence across reboot | Nothing persists |
| 7 | Search fields | Title, artist, album — not genre |
| 8 | Encoder in search | Results only; typing is touchscreen-only |
| 9 | Unreadable-medium toast | Yes, once on insert |
| 10 | Covers on medium rows | No — text and icons |
| 11 | Sort fields, in order | Default, BPM, Key, Title, Artist, Genre, Album, Date added, Label, Year, Duration, Rating |
| 12 | Rating and colour | Rating in the info panel; colour as a stripe on the row's left edge |
| 13 | POWER | Root-menu row at the bottom, confirmation dialog kept, removed from the deck header |
| 14 | Menu bar | Hidden; hover-only on a real mouse at the top edge (§4.4) |
| 15 | Stick pulled while playing | Track cache (§12): the loaded track is always cached, the toast says so, and the serve side keeps feeding a consuming CDJ |
| 16 | Parser unification | Part of this project |
| — | Tap the selected row | Info layout in a track list; **load** in search results; go in, everywhere else |
| — | Long press a row | Load / activate, same as encoder push |
| — | Info layout left rows | One line: title left, sort value (or artist) right |
| — | Sort menu by touch | Scroll and select by touch; tap outside to dismiss. The breadcrumb's indicator flips the direction (§9.1.1) |
| — | Missing cover art | Mixxx's own placeholder, in the row and in the info panel — the same square the deck's header falls back to |
| — | Album and Label rows | Carry the first available cover under that value |
| — | Key colouring | Green when harmonically compatible with the playing key, exact match included (§8.3) |
| — | Tap above a track list | A single tap goes in — menus are cheap to enter and free to leave |
| — | BPM tiling | Anchored: buckets tile outward from the reference BPM |
| — | Cache tiers | RAM (tmpfs, 1 GB) first; the SD card only for bytes that can no longer be re-read (§12.2) |
| — | Serve side | Whole-file cache on first peer read; phantom medium on stick loss; deferred clean unmount (§12.5) |

## 17. Left to measure, not to decide

Every design question is answered. Two things are settled by instrumentation
rather than by argument, and neither blocks starting.

1. **Tier 1's real ceiling** (§12.3). 1 GB is derived from one measurement of a
   deck at rest with a track loaded. Re-measure with two sticks mounted, a
   remote medium read, and a set's worth of prefetching behind it, and watch
   `/proc/swaps` — the moment zram takes a page, tier 1 is too big. The
   diagnostics page (§14) exists partly to make this observable without SSH.

2. **How to hide a phantom medium** (§12.5). Keeping a peer's mount alive while
   making the medium unbrowsable is the one behaviour here that is guessed
   rather than known. Build the straightforward version, then capture a session:
   load a track from us on a CDJ-2000NXS, pull our stick, and watch what the
   player asks for and what it draws. The capture decides whether "report it
   empty" is enough or whether the medium has to keep answering browse queries
   with a stale listing.
