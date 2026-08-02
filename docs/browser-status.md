# The Browser — where this got to

Written at the end of an autonomous session. Read this before the plan.

## The headline, unvarnished

**The browser is not built. There is nothing new to look at on the deck.**

What exists is the layer underneath it — the schema, the single ingest, the
model, the query catalogue, the label resolver — compiling and committed. What
does not exist is any of the UI: no source list, no medium menu, no track list,
no sort menu, no keyboard, no toasts, no diagnostics page, no track cache.

I did not get through the PRD. Stage 1 of ten is done and Stages 2–10 are not
started. The estimate in the plan was four weeks of focused work; this session
was a few hours, and the ordering was deliberate — the foundation first, so the
UI has something true to stand on — but the result is that the visible half is
still ahead.

I stopped writing code while the tree still **builds**, rather than leaving a
few thousand lines of never-compiled UI behind. A broken tree would mean the
deck could not be deployed to at all, and you could test nothing.

## What you can test

Short list, because the browser is not in it.

1. **`pi_config/deck-shot`** — grabs the deck's screen over ssh. Run it; a PNG
   should open.
2. **`pi_config/deck-poke`** — drives the deck from your desk.
   - `deck-poke tap 130 331` — a fingertip on a pixel.
   - `deck-poke browse down 3` / `browse up 3` — three encoder detents.
   - `deck-poke push`, `back`, `sort`, `play`, `cue`.
   - `deck-poke swipe 100 300 700 300`, `flick`, `longpress`.

   All of it verified working against the current library view during the
   session. The MIDI verbs go in through Mixxx's own controller port and run the
   real `TriMixxx.midi.xml` and `TriMixxx.scripts.js`, so they exercise the whole
   chain bar the copper.

   **One property worth checking deliberately:** `deck-poke` must never be able
   to make the Qt menu bar appear. It cannot hover, only press, and the menu
   bar's reveal is gated on hovering precisely so a fingertip cannot trigger it
   (PRD §4.4). If a tap ever pops the bar out, that gate is broken.

3. **The deck itself is unchanged and still works.** The binary now on it is the
   one that was there before; the data-layer commit compiles but has not been
   deployed, because nothing calls it yet.

## Implemented but not tested

Everything in `mixxx/src/library/deck/`. It compiles, and nothing executes it —
there is no call site yet, so **none of the following has ever run**:

| Piece | What is untested about it |
|---|---|
| `pdbingest` — schema + the single writer | Never ingested a real pdb. The SQL is modelled closely on `prolinkdbwriter`, which works, but the column list changed. |
| `deckqueries` — the whole browse hierarchy | Every query is unexecuted. The riskiest are the two using window functions (`search`, `lastPlayed`) and the playlist query's three correlated subqueries. |
| `decktrackmodel` | The temporary-view mechanism is copied from `BaseExternalPlaylistModel`, which works, but `setQuery()` has never been called. |
| `bpm::buckets` / `densityPeak` | Pure functions, hand-checked only. The tiling maths is the part I would test first. |
| `key::isCompatible` | Camelot conversion (`openKey + 6 mod 12 + 1`) is derived, not verified against a real key list. |
| `volumelabel` | Both paths unexercised. The sidecar it prefers **is not written yet** — the `dj-usb` change is not made — so today it would always fall through to `/dev/disk/by-label`. |
| `deck_history` / `deck_play_log` tables | Created but never written. History needs a `prolink-cxx` bridge change that is not done. |

Nothing ProLink-specific was written this session, so there is no blind remote
work for you to test. The remote path still runs the old code.

## Not implemented at all

Stages 2–10 of the plan, in full: the media registry and read-on-detection, the
browser widget and every level of it, categories, sort menu, info layout, search
keyboard, toasts, the hover-only menu bar, the `Shut down` root row, the track
cache, the serve-side phantom medium, and diagnostics.

The PRD and plan are unchanged and still accurate; this is a progress report
against them, not a revision of them.

## Findings from the hardware

Three things worth knowing, all discovered rather than assumed.

**Your second USB port could not enumerate a device.** Before you replugged it,
`usb2-port2` was cycling `"Cannot enable. Maybe the USB cable is bad?"` →
`"unable to enumerate USB device"` → power cycle, every four seconds, forever.
The device was electrically present and never came up. It worked after a
replug, so it is intermittent rather than dead — which on a USB 3 port usually
means signal integrity on the differential pairs, i.e. the internal cabling or
the connector, not the stick. Worth watching, because it fails silently: the
stick simply never appears.

**The mount slot is not the name, and on this deck they are crossed.**
`/media/DJ_USB_1` is the stick labelled `SAM2`; `/media/DJ_USB_2` is `SAM1`.
Slots are handed out in plug order. The old view showed the mount point, so it
was showing the wrong name about half the time — which is the concrete case
behind PRD §11.2.

**Mixxx's memory, measured with a track loaded:** 421 MB RSS of 3796 MB total,
508 MB used system-wide, 3288 MB available. `/tmp` is already tmpfs at 1.9 GB.
Swap is 2 GB of zram (`rpi-swap`, zram + writeback file), untouched. The
ProLink download cache currently resolves to `~/.cache/mixxx/prolink`, **on the
SD card** — so every remote track fetched today is a card write. These are the
numbers behind the cache budget in PRD §12.3.

## Notes for whoever picks this up

**The build loop is fast — much faster than the plan assumed.** Your Mac is
arm64 and Docker's server is arm64, so `mixxx/upload.sh` builds natively, and
the Dockerfile already cache-mounts `/build`, `/ccache` and the cargo registry.
An incremental compile error surfaced **19 seconds** in. Iterate freely.

**Every `Q_OBJECT` class must `#include "moc_<file>.cpp"` at the end of its
.cpp.** Mixxx keeps `mocs_compilation.cpp` empty and asserts it in
`src/util/moc_included_test.cpp`. Forgetting it fails the build with
`"mocs_compilation.cpp not empty"`, which names neither your file nor your
class. This cost one build cycle and will cost one per new widget.

**Watch the background-task exit code.** A backgrounded `docker build` whose
output is redirected reports the *wrapper's* status, not the build's. The first
data-layer build looked green and had actually failed; the truth was in the log
file. Grep the log for `error:`, do not trust the notification.

## The continuation point

In order, smallest first:

1. **Make the ingest run.** Call `createTables()` at startup and
   `writeMedium(read_pdb(mountPoint + "/PIONEER/rekordbox/export.pdb"), ...)`
   for each `/media/DJ_USB_*`. This is the first moment any of the new code
   executes, and it can be proved with `sqlite3` on the deck's database before a
   single pixel is drawn — which is the cheapest possible verification of the
   schema, the ingest and half the query catalogue.
2. **Then the registry** (`MediaRegistry`, `MediumReader`) and the `dj-usb`
   sidecar, so labels and counts are real.
3. **Then the browser widget**, which is where the plan's Stage 3 begins and
   where `deck-shot` starts earning its keep.

The `deck-poke`/`deck-shot` pair is the thing to lean on from step 3 onward: it
turns "does this look right" from a walk across the room into a shell command.
