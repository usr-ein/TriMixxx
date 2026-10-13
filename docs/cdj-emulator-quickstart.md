# Emulated CDJs: a quickstart

A CDJ-2000NXS running Pioneer's own firmware, emulated on the Mac. It joins
the Pro DJ Link network of emulated TriMixxx decks, so ProLink can be tried
without the booth: a deck next to "real" CDJs, and CDJs with each other.

This page takes you from nothing to a CDJ on your screen, then a CDJ next to
a TriMixxx deck, then two CDJs. Why the emulator is built the way it is, and
the rest: [cdj-emulator.md](cdj-emulator.md).

## What you need

- **This Mac,** or one set up as `pi-qemu/README.md` says ("Setting up a
  laptop"), with `uv`. The CDJ needs nothing more: it builds with what
  pi-qemu already uses.
- **No Docker.** Docker builds Mixxx and the decks' cards, not the CDJ, and
  an emulated deck starts from the golden snapshot this Mac already has.
- **The firmware:** Pioneer's updater for the CDJ-2000NXS, version 1.44,
  `C2KNXS.UPD`, which Pioneer gives the NXS's owners. It never goes into a
  repository, and neither does anything made from it, screenshots of the
  CDJ's screen included.
- **Two free cores for each CDJ,** for as long as it runs.

## Once: set it up

In the main checkout:

```sh
git submodule update --init cdj2000-emulator   # the emulator, at the commit this checkout records
pi-qemu/build.sh app                           # pi-qemu itself, with every `cdj` verb: about a minute
pi-qemu cdj build                              # the emulator, in cdj2000-emulator/build: ~1.5 min the first time
pi-qemu cdj firmware ~/Downloads/C2KNXS.UPD    # once per Mac: extracted into ~/.pi-qemu/cdj/firmware/
```

- **Whenever main has moved,** by a pull or a merge, run the first three
  lines again: neither moves the emulator or rebuilds pi-qemu by itself. The
  builds redo only what changed.
- **In a git worktree,** `pi-qemu worktree prepare` stands in for the first
  line: it lends the worktree the main checkout's emulator. There,
  `pi-qemu/build.sh app` builds the worktree's own pi-qemu,
  `pi-qemu/app/build/pi-qemu`, which you run by that path: the `pi-qemu` on
  PATH is the main checkout's.
- **A step that fails:** see "Troubleshooting", at the end.

## A first CDJ, in a window

```sh
pi-qemu cdj up a --window
```

```text
a: player screen up in 8 s
a is up (booted in 8 s): pid 61652, no cable in, in a window
```

- **It boots every time,** in about 8 s: a CDJ is never restored from a
  snapshot, as a deck is.
- **The window** is pi-qemu's drawing of the NXS's top panel, around the
  CDJ's live screen. The screen says "Not Loaded." over the rekordbox logo,
  with PLAYER 1 and TEMPO 0.00 %. Around it:
  - BROWSE, TAG LIST, INFO and MENU above the screen; the sources
    (rekordbox, LINK, USB, SD, DISC) down its left; the selector, BACK and
    TAG TRACK on its right
  - the hot cues and the loop keys below it
  - CUE and PLAY left of the jog; SYNC, MASTER and the tempo keys right of
    it; the TEMPO fader at the far right, minus at the top
- **The pads light as the CDJ's firmware lights its lamps.** At first the
  sources, the browse keys, SYNC and MASTER glow dimly (they are available),
  LOOP IN and LOOP OUT are orange and QUANTIZE is red.
- **Clicking:** a pad stays down while the mouse button does; shift-click
  latches it. Drag or scroll the selector to turn it, click it to push it.
  On the keyboard: space is PLAY, C is CUE, the up and down arrows turn the
  selector, return pushes it, backspace is BACK.
- **The jog is not emulated,** only its touch.
- **Closing the window leaves the CDJ running.** `pi-qemu cdj up a --window`
  opens it again.
- **To stop it:** `pi-qemu cdj rm a`. It stops the CDJ, closes its window
  and deletes `~/.pi-qemu/cdj/a/`. A CDJ left running holds its two cores
  for a day, then stops by itself.

## Next to a TriMixxx deck

```sh
pi-qemu deck up d --link booth              # an emulated deck, on the link "booth": ~15 s
pi-qemu cdj up a --link booth --window      # a CDJ on the same link: ~20 s
pi-qemu link devices booth 10               # who announces itself there, heard for 10 s
```

```text
player  name                  address           MAC                deck
1       CDJ-2000nexus         169.254.x.y       02:43:44:..        a
4       CDJ-2000nexus         169.254.x.y       02:54:4d:..        d
```

- **That is them finding each other:** the CDJ took player 1, the deck
  player 4. A TriMixxx deck announces itself as a CDJ-2000nexus too: the
  last column says which is which. `cdj up` prints the CDJ's number as well:
  `a: player 1 at 169.254.x.y, 02:43:44:.. on link booth`.
- **Give `link devices` 10 s.** With a TriMixxx deck on the link, a CDJ
  claims its number again every few seconds, as a real one did, so its
  keep-alives come up to ~5 s apart.
- **The deck's screen:** `pi-qemu deck shot d d.png`. To watch it live,
  start the deck with `--window`: a running deck takes no new options.
- **A link is a name.** Decks and CDJs on the same one share a network; a
  CDJ on none has no cable in. On one link, a deck and a CDJ can't share a
  name.
- **When done:** `pi-qemu cdj rm a`, then `pi-qemu deck rm d`. `deck rm`
  leaves CDJs running.

## A stick, and a track that plays

A CDJ takes a FAT32 image as its USB stick (`--usb`) or its SD card
(`--sd`). To make one, first copy a stick's files into a folder, in the
Finder or with `cp -R`: never give the emulator the stick itself. Then make
the image from the folder, and start a CDJ with it:

```sh
(cd cdj2000-emulator && .venv/bin/python -m tools.cdj_main.make_sd_image ~/cdj-sticks/mine ~/cdj-sticks/mine.img --size 4G)
pi-qemu cdj up a --usb ~/cdj-sticks/mine.img --dsp-model --window
```

- **The image takes seconds.** Its size is a power of two, bigger than the
  files. It is sparse: on disk it takes only the files' room. Keep stick
  folders out of `~/.pi-qemu/cdj/`: a CDJ named like a folder there takes it
  for its own, and `cdj rm` deletes it.
- **A rekordbox stick** gives the CDJ its library: BPMs, beat grids,
  waveforms. A deck lists it, and two CDJs can sync on it. A stick of plain
  audio files is browsed by folder, and its tracks have no BPM.
- **The CDJ opens the stick by itself,** about 20 s after it starts: its USB
  pad lights, and its browser shows the stick's categories (a rekordbox
  stick) or its folders.
- **To load a track,** turn the selector and push it, down through the
  categories or folders to the track; a push on the track loads it. With
  `--dsp-model` it plays at once: PLAY lights green and the time counts
  down.
- **The CDJ's writes are thrown away.** Its history and settings go to an
  overlay deleted when it stops. The image never changes, so two CDJs can
  share one.
- **`--dsp-model` makes a loaded track play.** Without it, the CDJ runs
  Pioneer's own DSP code, interpreted, far too slowly to play: the track
  stays cued on its screen, though about 4 s after the load the CDJ tells
  the link it is playing, PLAY unpressed. With `--dsp-model`, the emulator's
  model of the DSP keeps the track's position in real time, and the CDJ
  sends its beats at the track's tempo.
- **Neither makes a sound.** And with `--dsp-model` a track plays the moment
  it loads, with no cue and no PLAY; CUE then does nothing while it plays,
  until the deck has been stopped and cued once.
- **`--test-track`** gives the CDJ an SD card with a 10 s test WAV instead.
- **When done:** `pi-qemu cdj rm a`.

## Two CDJs: master and sync

With a deck on the link too, to see what it sees:

```sh
pi-qemu deck up d --link booth
pi-qemu cdj up a --link booth --usb ~/cdj-sticks/mine.img --dsp-model --window
pi-qemu cdj up b --link booth --usb ~/cdj-sticks/mine.img --dsp-model --window
```

With a rekordbox stick, in the two windows:

1. **Load a track on each.** The first to play takes master, as on real
   NXSs: its MASTER pad lights, and the other's screen says MASTER PLAYER 1.
2. **Double-click each TEMPO fader,** to centre it. Until its fader first
   moves, an emulated NXS tells the link its pitch is -100 %, and a CDJ
   synced to it shows 0.0 BPM.
3. **SYNC on `b`:** its SYNC pad lights, and its BPM becomes `a`'s.
4. **MASTER on `b`:** `b`'s MASTER pad lights, and `a`'s goes back to a dim
   glow.

The lamps as text, for a script: `pi-qemu cdj lamps b` says
`"MASTER": "3"` and `"SYNC": "3"` (lit), and `"1"` for the dim glow of a key
that is available.

The deck lists both CDJs' sticks, as `1 <label>` and `2 <label>`:

```sh
pi-qemu deck press d push          # opens its library
pi-qemu deck shot d d.png
```

When done: `pi-qemu cdj rm a`, `pi-qemu cdj rm b` and `pi-qemu deck rm d`.

## From the command line

Everything the window does, the command line does too, and both can drive
one CDJ at once:

```sh
pi-qemu cdj press a play            # a panel key, for 100 ms: play, cue, link, usb, sd, enter, back, menu, sync, master, ...
pi-qemu cdj press a cue --hold-ms 500
pi-qemu cdj rotary a 2              # the selector, two rows down; -1 is one row up
pi-qemu cdj lamps a                 # the lamps its firmware lit, by name, as JSON
pi-qemu cdj shot a a.png            # its 480x234 screen
pi-qemu cdj status a                # the emulator's own view: "Latest browser reply: Not Loaded.", ...
pi-qemu cdj list                    # every CDJ, running or not
pi-qemu cdj rm a                    # stop it, and delete its state
```

- **Key names** are the NXS's own, in lower case with a dash for a space:
  `hot-cue-a`, `tempo-reset`, `master-tempo`, ... `enter` is the selector's
  push and `back` is BACK. A name it doesn't know, it answers with the list.
- **Turn the selector a few rows at a time:** a large `rotary` can lose
  some (13 has moved it 8 rows).
- **The TEMPO fader has no verb:** double-click it in the window. Without
  one, the `trimixxx0` skill (`.claude/skills/trimixxx0/SKILL.md`) has the
  emulator's own command.

## What is not like a real CDJ

- **No sound, and no jog,** but for its touch-sensitive top.
- **A track plays only with `--dsp-model`,** and then from the moment it
  loads, where a real NXS cues it and waits for PLAY. Without it, the screen
  stays cued, but about 4 s after the load the CDJ tells the link it is
  playing.
- **Its TEMPO fader starts uncentred:** its pitch is -100 % on the link
  until the fader first moves.
- **SYNC bends into phase** over 4 to 7 beats, up to 10 points off its
  pitch, where a real NXS jumps into phase on its next beat.
- **It boots every time,** about 20 s to the link, and stops after a day.
- **It keeps a real NXS's timing** (keep-alives every 2.0 s) while the Mac
  has cores to spare: with every core busy, they slip to 2.1-3.2 s.

The measurements against real NXSs, and the rest:
[cdj-emulator.md](cdj-emulator.md), "Limits", and
`mixxx/lib/prolink/emu-captures/README.md`.

## Troubleshooting

| You see | What to do |
|---|---|
| `pi-qemu cdj: no verb window` (or `lamps`), or `unknown option --dsp-model` | The `pi-qemu` on PATH is older than them: `pi-qemu/build.sh app` in the main checkout. |
| `no cdj2000-emulator checked out here` | `pi-qemu cdj build` checks it out. |
| `no cdj2000-emulator in this worktree` | `pi-qemu worktree prepare`. |
| `the emulator is not built in this checkout` | `pi-qemu cdj build`. |
| `no NXS firmware in ~/.pi-qemu/cdj/firmware/nxs` | `pi-qemu cdj firmware ~/Downloads/C2KNXS.UPD`. |
| `patch does not apply cleanly: .../qemu-sh-tmu-catch-up.patch`, or `... carries a different or unrecorded version of qemu-...patch` | The checkout's QEMU was patched for another commit of the emulator. Reset it once, then build again (below). |
| `a is already running: its options cannot change now` | `pi-qemu cdj rm a`, then `up` again with the new options. |
| `a: no player screen in 120 s` | `up` stopped it, and kept its logs in `~/.pi-qemu/cdj/a/` (`pi-qemu.log`, `run/`). The Mac may be too busy: `pi-qemu cdj list`, `pi-qemu deck list`. `cdj rm a` deletes the logs. |
| A CDJ missing from `link devices` | Give it 10 s: `pi-qemu link devices booth 10`. |
| A CDJ in SYNC shows 0.0 BPM | The master's TEMPO fader never moved: double-click it. |
| A key stays down after a window crashed | Open the window again, and click that key once. |
| `CDJ a runs from this checkout's cdj2000-emulator` (`worktree release`) | `pi-qemu cdj rm a` first. |
| The Mac crawls | A forgotten CDJ holds two cores for a day: `pi-qemu cdj list`, then `cdj rm`. |

The reset, from the checkout's root:

```sh
git -C cdj2000-emulator/build/qemu checkout -- . && git -C cdj2000-emulator/build/qemu clean -fd -e build
pi-qemu cdj build
```
