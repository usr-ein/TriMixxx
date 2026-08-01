# doom — the deck plays DOOM (1993)

**Hold PLAY while the deck boots, and it comes up as Doom instead of Mixxx.**

The jog wheel turns. The tempo fader is the throttle. The seven pads of ring A
are the seven weapons. Ring B is the menu. Play is fire.

```
hold PLAY at power-on ──▶ launchd ──▶ /run/trimixxx/mode = doom ──▶ ~/.xinitrc ──▶ chocolate-doom
                                          │
                                          └─▶ trimixxx-deckkeys --map doom
                                                    │
                          deck MIDI ────────────────┴──▶ /dev/uinput ──▶ a keyboard + a mouse
```

## How this works at all

Doom is **unmodified** — a stock, packaged `chocolate-doom`. There is no engine
fork in this repo, no MIDI support in the game, no patched input layer.

The trick is one level down: [`trimixxx-deckkeys`](../trimixxx-launcher) reads
the deck's MIDI (the same ALSA stream Mixxx normally uses) and replays it into
the kernel through `/dev/uinput` as a **virtual USB keyboard and mouse**. From
Doom's point of view, somebody is playing on ordinary hardware.

Three things fall out of that, all of them the reason it was done this way:

- **Nothing to maintain.** The engine is `apt install`. The mapping is a Go
  table ([`internal/keymap/doom.go`](../trimixxx-launcher/internal/keymap/doom.go)),
  so changing what a pad does is a table edit, not a port.
- **The jog wheel can be an analogue turn axis.** No keyboard can express "turn
  seven degrees"; a mouse can, and a jog wheel *is* a rotary control. This is
  the one input on the deck that is better than a real keyboard.
- **The same device serves the rescue console.** The deck has no keyboard port.
  A virtual keyboard driven from its own buttons is what makes a debug screen
  usable at all — see `--map console`.

## Controls

`trimixxx-deckkeys --map doom --print-map` prints this from the actual table, so
it can never quietly drift from what the deck does:

| Deck control | Sends | Light | Does |
| --- | --- | --- | --- |
| **jog wheel — touch it** | ctrl | — | **fire** |
| **jog wheel — turn it** | mouse X | — | **turn** |
| **tempo fader** | ↑ / ↓ / +shift | — | stand still, walk, run — forward and back |
| **play** | `]` | lit | next weapon |
| **cue** | space | lit | use / open door |
| loop in / loop out | `,` / `.` | lit | strafe left / right |
| reloop | shift | *no LED* | run (hold) |
| track encoder | ↑ / ↓ | — | menu up / down |
| encoder press | enter | — | select |
| ring A, pads 1–7 | 1–7 | 7 colours | fist, pistol, shotgun, chaingun, rockets, plasma, BFG |
| **ring B, pad 1** | ↓ | **red** | **walk backward** / menu down |
| **ring B, pad 2** | ↑ | **green** | **walk forward** / menu up |
| ring B, pad 3 | esc | yellow | menu / back |
| ring B, pads 4–5 | ← → | blue / cyan | menu left / right, turn |
| ring B, pad 6 | y | white | confirm quit |

**Fire is the jog wheel's touch sensor.** Rest a hand on the platter and the gun
goes off — the same hand that aims is the one that shoots, which means you
cannot turn without firing. That is the point, not an oversight.

**Red walks backward, green walks forward**, on two adjacent ring B pads,
because that is what those colours mean everywhere else. It costs nothing to
honour: Doom's walk-forward/back and its menu cursor are the *same two keys*, so
one pad is both "walk forward" and "menu up" with no ambiguity.

Ring A has exactly seven populated nodes and Doom has exactly seven weapons.
Ring B has six, and a menu needs exactly Esc, four cursors and a confirm. That
is a coincidence, and it was too good to waste.

**The tempo fader as a throttle** is the other one worth explaining: a
centre-detented linear fader is already a forward/back speed lever. Past the
deadzone it walks, past 60 % of travel it runs. The deadzone is not optional —
the fader is a bare pot on an ADC and its centre wanders by a few counts, which
without it would mean drifting into a wall while standing still.

**Weapon cycling is PLAY, and it taps `]` rather than a digit.** That is a fix
rather than a preference: a digit selects one specific weapon, and selecting a
weapon you do not own does nothing at all — so at the start of E1M1, where five
of the seven are missing, cycling the digits produced no visible change and the
control felt dead. (A passive `--dry-run` probe on the live deck confirmed the
MIDI was arriving and the digits were being pressed correctly the whole time;
the game was simply ignoring them.) Doom's own weapon cycling skips what you are
not carrying, so every press does something. It is the one binding here that
Chocolate Doom leaves unbound by default, which is why `default.cfg` sets
`key_prevweapon` / `key_nextweapon`.

## Lights

The deck paints itself when it becomes a Doom controller, and goes dark again
when it stops. `trimixxx-deckkeys` does it from the same table the keys come
from, repainting every few seconds.

**Why repainting, and not once at startup.** Switching modes starts the key
bridge while the *outgoing* Mixxx is still being SIGTERMed — and Mixxx's
controller script blanks every ring LED on its way out (`TriMixxx.shutdown`,
"Clear every ring button LED"). A single paint lands a second or two before
that and gets wiped, which looks exactly like a deck that never lit up. It was
this bug, and the first theory (that a 143-byte burst of SysEx was overrunning
the S3's UART) was wrong: measured on the deck, a burst lights it exactly as
well as the same messages spaced 25 ms apart. Repainting also heals an S3 that
has been reset mid-game, and costs ~155 bytes every few seconds on a link that
carries more than that from one flick of the jog wheel.

**Ring A — the weapons.** Ramped by how much they hurt, except the last three,
which take the colours the game itself uses:

| Pad | Weapon | Colour | |
| --- | --- | --- | --- |
| 1 | fist / chainsaw | white | `#FFFFFF` |
| 2 | pistol | yellow | `#FFBE00` |
| 3 | shotgun | orange | `#FF5000` |
| 4 | chaingun | red | `#FF0000` |
| 5 | rocket launcher | violet | `#A000FF` |
| 6 | plasma rifle | cyan | `#00B4FF` |
| 7 | BFG 9000 | green | `#00FF00` |

**Ring B — the menu.** One colour per direction, so a cursor key can be picked
out without counting round the ring:

| Pad | Function | Colour | |
| --- | --- | --- | --- |
| 1 | **walk backward** / menu down | red | `#FF0000` |
| 2 | **walk forward** / menu up | green | `#00FF00` |
| 3 | esc / back | yellow | `#FFBE00` |
| 4 | left / turn | blue | `#003CFF` |
| 5 | right / turn | cyan | `#00FFB4` |
| 6 | confirm quit (`y`) | white | `#FFFFFF` |

The two rings repeat white, red and green between them. They are physically
separate rings on opposite sides of the deck, so there is nothing to confuse;
*within* a ring every colour is unique, and a test enforces that.

**The single-colour lamps** — play, cue, loop in, loop out — can only be
switched on, not coloured, so they simply light to say "this one does
something". **Reloop stays dark whatever you do**: it has a button and a note
but no LED behind it on the loop board. Nothing to fix in software.

Colour travels as SysEx (`F0 7D 01 <node> …` for ring A, `03` for ring B), with
each 8-bit channel split into two nibbles because SysEx payload bytes must be
7-bit — a single byte would cap every channel at half brightness.
`trimixxx-deckkeys --lights=false` turns the whole thing off.

### Getting out

Everything below works from the deck alone, no keyboard:

- **Esc → Quit Game → y** — ring B pad 1, cursor down, encoder press, ring B pad 6.
- **Panic chord: hold LOOP IN + LOOP OUT together for 2 s.** Strafe left and
  strafe right at once — the one combination nobody holds by accident, because
  they cancel out. The launch manager sees it directly off the MIDI and puts the
  deck back into Mixxx.
- **Power cycle.** The mode lives on tmpfs, so a reboot is always Mixxx.

When Doom exits normally, `~/.xinitrc` starts Mixxx in the same session — Doom
is a one-shot, and cannot loop.

## Install

```sh
./fetch-wad.sh          # downloads doom1.wad (shareware) into ./wad/
./install.sh            # engine + WAD + launcher + configs, onto trimixxx-pi
HOST=other ./install.sh # a different host
```

Then, from anywhere:

```sh
ssh trimixxx-pi 'echo doom | sudo tee /run/trimixxx/mode'   # switch now
ssh trimixxx-pi 'DISPLAY=:0 trimixxx-doom'                  # just look at it
```

### The WAD

`doom1.wad`, the **shareware episode** — which is what "can it run Doom?" has
always meant, and what everyone plays: E1M1, Hangar, that riff. It is not
committed here (4 MB of someone else's game), and `fetch-wad.sh` verifies what
it downloads: the `IWAD` magic always, and the published v1.9 hashes as a
warning if they do not match, since any of the six shareware releases plays.

| | |
| --- | --- |
| size | 4 196 020 bytes |
| md5 | `f0cefca49926d00903cf57551d901abe` |
| sha256 | `1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771` |

The shareware episode has been freely redistributable since 1993 — that was the
entire point of shareware. The **full** `doom.wad` is not; if you own it:

```sh
./fetch-wad.sh --from ~/games/doom.wad
```

Chocolate Doom takes any IWAD, so Doom II and Ultimate Doom work the same way.

### Sound

SFX work out of the box, into the UCA222 (Mixxx is not running in this mode, so
the card is free). **Music ships off**, deliberately: the config value that
selects OPL emulation versus native MIDI has moved between Chocolate Doom
builds, and guessing wrong is either silence or an error at startup. One command
fixes it properly, and writes the right number into the same file:

```sh
chocolate-doom-setup     # Configure Sound -> Music: OPL (Adlib)
```

OPL is the authentic 1993 sound and needs no soundfont or extra packages.

## Files

| File | What |
| --- | --- |
| `fetch-wad.sh` | downloads and verifies `doom1.wad` into `wad/` (gitignored) |
| `install.sh` | engine + WAD + launcher + configs onto the Pi |
| `trimixxx-doom` | the wrapper `~/.xinitrc` runs; finds the WAD, sets SDL up, execs the engine |
| `default.cfg` | Chocolate Doom's vanilla settings — **no `key_*` lines, on purpose** |
| `chocolate-doom.cfg` | fullscreen, aspect, mouse grab, no joystick |

**Why `default.cfg` binds no keys.** The deck's virtual keyboard is mapped onto
the keys Doom *already* binds by default. Key values in that file are Doom's own
internal codes — not Linux key codes, not ASCII — so hand-authoring them is the
easiest way in this whole feature to end up with a deck that presses nothing.
Bind to the defaults and there is nothing to get wrong; the mapping lives in
`doom.go`, which has tests.

## Status

Confirmed on the deck: Chocolate Doom **3.1.0** from apt, the WAD, the boot
gesture, the virtual keyboard and mouse under X, the jog wheel as the turn axis,
sound levels into the UCA222, and the lights — all 13 pads in colour and the 4
lamps. Tuned there too — `--jog-scale` is **1.8** px/tick (6 was far too fast to
aim with, and 3.6 still was) and `--jog-invert` is **on**, because the wheel's
quadrature counts the opposite way round from mouse X and without it left turns
right.

Still untested on hardware:

- `key_nextweapon` actually cycling from PLAY.
- Fire on the jog touch sensor.
- Which end of the tempo fader feels like "forward" (`--fader-invert` swaps it).
