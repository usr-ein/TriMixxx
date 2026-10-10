# pi-qemu: the TriMixxx tool

`pi-qemu` is the one tool for TriMixxx decks, emulated and real:

- **Emulated decks.** It runs a deck's SD card on an emulated Raspberry Pi 4
  (QEMU's `raspi4b`, accelerated by Apple's Hypervisor framework), booting the
  real Raspberry Pi OS card with the real deck software: Mixxx, ttymidi, the
  launcher and the panel session. You get the Pi's 1280x800 screen in a window
  (click and drag like the touchscreen), and a control panel standing in for
  the deck's S3 board. The panel sends MIDI to the Pi's UART exactly as the
  real S3 does.
- **Any deck.** It drives a deck from the command line, emulated or real: its
  controls, its touchscreen, its screen, Mixxx's recorder, a shell. Scripts
  and AI agents use the same commands.
- **Deploying.** It puts the repo's code onto a deck, step by numbered step
  (`deploy/NNN_*.sh`).
- **Cards and releases.** It builds a deck's card from stock Raspberry Pi OS,
  and the locked A/B release card with its updates over the air.

The emulated deck is called **trimixxx0**: a virtual trimixxx2, wired by
`mixxx_config/units/trimixxx0.json`. Why things are built the way they are is
in [PLAN.md](PLAN.md).

```sh
pi-qemu help                  # the groups: deck, link, image, release, worktree
pi-qemu deck                  # a group's verbs
pi-qemu deck up --help        # one verb, in full
```

Every verb that acts on a deck names it, as its **TARGET**: an emulated
deck's NAME (`pi-qemu deck list`), or `--host ALIAS` for a real deck by its
ssh alias. There is no default deck: nothing reaches a real deck unless it is
named.

A real deck on the Mac's switch is reached over Ethernet too, much faster
than over Wi-Fi: `pi-qemu deck alias --host trimixxx-pi` writes
`trimixxx-pi-eth` into `~/.ssh/config`, by its eth0's IPv6 link-local
address through the Mac's wired interface, and `--host trimixxx-pi-eth`
then takes that link.

## What is in here

| Path | What it is |
|---|---|
| `build.sh` | builds the pinned, patched QEMU and `pi-qemu`, and puts `pi-qemu` on PATH |
| `app/` | `pi-qemu` itself (C++/Qt), one directory per part under `app/src/`: `board/` (the emulated Pi: the firmware step, QEMU, USB sticks, its link, its control socket), `s3/` (the deck's controller: MIDI both ways, the wiring), `ui/` (the windows), `decks/` (a deck to act on, emulated or real), `pipeline/` (deploying, image builds, releases), `cdj/` (emulated CDJs), `worktree/`, `cli/` (the command line), `util/`; and `tests/` (ctest) |
| `app/icons/` | its icon: `trimixxx.svg`, and the `.ico` compiled in, made from it by `make-ico.py` |
| `qemu/trimixxx-patches.py` | our patches to QEMU, applied by `build.sh` |
| `deploy/` | the deploy steps, `NNN_name.sh` and `NNN_name.pi.sh`, and their contract, `lib.sh` |
| `image/` | the stock card's pin (`stock.env`), its first-boot seed (`user-data.in`, `network-config`), and `secrets.env` (gitignored; `secrets.example.env` is the template) |
| `release/` | the release container (`Dockerfile`, `container.sh`) and the release card's parts |
| `.cache/` (gitignored) | images, built cards, the golden snapshot, decks' identities, USB stick images |

## Setting up a laptop

On an Apple Silicon Mac:

```sh
brew install qemu mtools cmake ninja qt python@3.12 dtc   # QEMU's deps, mtools for FAT, Qt for the app
pi-qemu/build.sh             # ~10 min the first time: QEMU 11.1.0 + patches, dtmerge, then pi-qemu
cp pi-qemu/image/secrets.example.env pi-qemu/image/secrets.env   # then set SAM1902_PASSWORD
```

`build.sh` puts `pi-qemu` on PATH as `~/.local/bin/pi-qemu` (from the main
checkout; a worktree's own build is run by its path). `build.sh app` builds
`pi-qemu` alone, with its tests.

You also need Docker Desktop running (the deck's arm64 binaries are Docker
builds) and the ssh key the cards trust: `~/.ssh/no_pass/rsa_sam`, else
`~/.ssh/with_pass/rsa_sam` (in the agent: `ssh-add`), or `SSH_KEY=` pointing
at another one. Its public half goes onto the card at build time.

Then build the card. Most of the time goes to apt and the Docker builds of
the deck's binaries; the base stage is cached for later runs:

```sh
pi-qemu image build              # -> pi-qemu/.cache/build/trimixxx0.img (+ the golden snapshot)
```

A window shows the build as it goes: its steps, a progress bar and the log
(`pi-qemu build-log pi-qemu/.cache/build/trimixxx0.log`, the log it writes).
`release build` opens one too, for the whole release. `--no-window` (or
`BUILD_WINDOW=0`) opens none.

## Running your deck

Your own deck is an emulated deck like any other, with windows:

```sh
pi-qemu deck up sam --window     # restored from the golden snapshot in ~3 s
pi-qemu deck ssh sam
```

Two windows open: the Pi's screen and the control panel. Log in on the
console as `sam1902` with the password from `image/secrets.env`, or use
`deck ssh`. `deck up sam --from CARD` boots a card of your own instead.

**Sound is off unless you ask for it.** `pi-qemu deck up sam --window --
--audio speakers` plays the deck's output on the Mac. `-- --audio
wav:/abs/path/out.wav` records it to a file instead, which is how to check
audio without hearing it. Whatever follows `--` goes to `pi-qemu run`, the
board itself (`pi-qemu run --help` lists its options):

| Option | |
|---|---|
| `--audio none\|speakers\|wav:FILE` | the deck's sound output (default none: silent) |
| `--display window\|vnc\|none` | the Pi's screen; screenshots work in every mode |
| `--stick FILE.img` | a USB stick image plugged in from power-on |
| `--deck NAME` | whose wiring the panel follows (`mixxx_config/units/NAME.json`) |
| `--ssh PORT` | the Pi's ssh on 127.0.0.1:PORT (default: a free port) |
| `--link NET` | eth0 on the link NET, shared with other boards (`deck up --link` sets it, and `--name`, `--mac`) |

### The panel

- **Controls tab**: the deck's controls, laid out as on trimixxx2.
  - **Pads:** loop and loop-length pads across the top; SLIP, SORT and the four
    hot cues left of the platter; CUE and PLAY / PAUSE under them.
  - **Right of the platter:** BACK, the browse encoder (drag up or down to turn,
    click to push), TEMPO RANGE, MASTER TEMPO and the tempo fader.
  - **Jog wheel:** drag the inner disc to scratch (touch), drag the rim to
    bend, or scroll on it to nudge.
  - **Pad behaviour:** shift-click a pad to latch it down. The lamps and pads
    light from what Mixxx sends back.
- **S3, power, USB sticks tab**:
  - the S3 link light;
  - Power on, and Pull the plug (no shutdown, like the mains switch);
  - the sound mode;
  - every USB storage device on the Mac, and every image and `.stick` file
    in `.cache/sticks/`, each with Insert / Unplug (two slots, read-only).

### Driving it from the command line

Everything the panel does, `pi-qemu deck` does too, on any deck: an emulated
one through its virtual S3, a real one (`--host ALIAS`) straight into the MIDI
port Mixxx opened for the deck's controller. The bytes are the same either
way, on the deck's wiring, so they run the real mapping and scripts.

```sh
pi-qemu deck press sam play          # press and release (80 ms; `press sam cue 500` holds it)
pi-qemu deck down sam cue; pi-qemu deck up sam cue
pi-qemu deck jog sam 3240            # a quarter turn clockwise (12960 ticks per turn)
pi-qemu deck touch sam on            # the platter's touch sensor, for scratching
pi-qemu deck browse sam -3           # the track encoder; + is up
pi-qemu deck tempo sam center        # the fader, 0..16383; 8192 (center) is the middle
pi-qemu deck midi sam 90 3C 7F       # raw bytes, as the S3's
pi-qemu deck shot sam shot.png       # the screen: QEMU's own (emulated), scrot (real)
pi-qemu deck tap sam 640 400         # the touchscreen, with xdotool: also longpress, swipe, flick, key, where
pi-qemu deck record sam 10 mix.wav   # Mixxx's own recording of its main mix, fetched
```

Control names: `tempo-range keylock loop8 loop4 loop-double loop-halve back
hotcue1..4 slip sort play cue loop-in loop-out reloop push jog-touch`. They
follow the deck's wiring, so `press sam hotcue1` sends whatever note
trimixxx0's hot cue 1 is on.

An emulated deck also has its board:

```sh
pi-qemu deck leds sam                # what the deck's lights show now, as JSON
pi-qemu deck stick sam list          # stick sam insert SAM3 | stick sam unplug SAM3
pi-qemu deck status sam              # the Pi, the S3 link, its wiring, its ports
pi-qemu deck power sam off           # pull the plug; power sam on
pi-qemu deck save sam FILE           # the whole machine to FILE, then off
pi-qemu deck console sam 'ip -br a'  # over the serial console, when ssh is down
```

### Where things are

An emulated deck lives in `~/.pi-qemu/instances/NAME/` (kept short on
purpose: macOS caps unix socket paths at 104 bytes): `card.img`, `state`
while it is suspended, `ssh_config` and `bin/` (plain `ssh`/`scp` reaching it
as NAME and as `deck`: `eval "$(pi-qemu deck env NAME)"`), `pi-qemu.log`, and
the board's run directory, `card.run/`:
- `qemu.cmd`: the exact QEMU command, to rerun by hand;
- `qemu.log`, and `qemu.pid` while it runs;
- `console.log`: the Pi's serial console, from the first kernel line;
- `ssh.port`;
- the sockets `control.sock` (what `pi-qemu deck` talks to), `console.sock`
  (the Pi's console: `nc -U console.sock`), `qmp.sock` and `qmp-fd.sock`
  (QEMU's monitor) and `s3.sock` (the S3's UART).

A deck on a link also has `link` in its directory (the link's name), and its
socket on the link is `~/.pi-qemu/links/NET/NAME.sock`.

Inside the Pi:
- Mixxx logs to `/tmp/mixxx/mixxx.log` and `/tmp/mixxx/stderr.log`; the
  previous run's are kept as `.1`;
- its settings are in `~/.mixxx/`;
- the launcher's log is `journalctl -u trimixxx-launchd`.

## Several decks at once

Emulated decks run side by side, for people, agents and tests. Each one has
its own copy of the card, ssh port, ssh alias and control socket. They are
headless and silent unless asked.

```sh
pi-qemu deck up mine                 # restored from the golden snapshot in ~3 s
pi-qemu deck ssh mine 'pgrep -a mixxx'
pi-qemu deck deploy mine config      # this checkout's code onto it; waits for the new Mixxx
pi-qemu deck ready mine              # wait until Mixxx runs, its sound and controller open
pi-qemu deck stop mine               # suspend: saved, next `up` ~3 s
pi-qemu deck rm mine                 # gone, card and all
pi-qemu deck list
```

`up --window` shows its screen and panel. `up --boot` boots it instead of
restoring it (~7 s to ssh). `up --fresh` starts again from the golden
snapshot, and `up --from CARD` boots a card of your own, of any size: the
instance's copy is rounded up to the power of two QEMU's SD card needs, and
stays sparse. `down` powers it off cleanly, `kill` pulls the plug (its QEMU
too, even if pi-qemu was killed outright).

The emulated board starts as a Pi 4 does, measured on a real one
(PLAN.md, phase 1):
- **The boot partition.** `autoboot.txt` picks it, with `[tryboot]` on a
  trial. With no `autoboot.txt`, it's the first FAT partition holding a
  `start.elf`. A guest's `reboot N` asks for a partition.
- **A trial (tryboot).** It's armed by
  `sudo systemctl reboot --reboot-argument="0 tryboot"` or
  `sudo vcmailbox 0x00038064 4 0 1`, used once, and forgotten at a power-off.
  A guest reboot pauses QEMU so pi-qemu can read it.
- **`config.txt`.** It takes `[boot_partition=N]`, `[partition=N]` and
  `[tryboot]`.
- **`/proc/device-tree/chosen/bootloader`** has the real firmware's values:
  `partition`, `tryboot`, and the rest.
- **`kernel_watchdog_timeout`** leaves the watchdog running, so a start that
  hangs before systemd resets the board.

Each emulated deck takes 2 GB of RAM; four at once is about the limit on a
16 GB Mac.

## Decks on one network: Pro DJ Link

Decks started on the same **link** have their eth0s on one network, as the
players in a booth share a switch. They see each other over Pro DJ Link,
browse each other's media, and load and play each other's tracks. A deck on
no link (the default) has its eth0 on a cable to an empty switch, so other
people's and agents' decks never meet yours.

```sh
pi-qemu deck up a --link booth --window    # a: player 4 on link booth
pi-qemu deck up b --link booth --window    # b: player 3 (it saw a holding 4)
pi-qemu deck stick a insert SANDISK-E02C   # a rekordbox stick into deck a...
pi-qemu deck press b push                  # ...is a source in b's library: "4 <its label>"
pi-qemu link devices booth                 # who announces what, heard from the Mac
```

- **On a link, each deck is a device of its own.** Its eth0 MAC comes from
  its NAME (`02:54:4d:..`), so it gets its own link-local address and claims
  its own player number, as a real deck does. `up` waits for that number and
  says it: `b: player 3 at 169.254.237.243, 02:54:4d:f0:62:70 on link booth`.
- **A restored deck is a clone until then.** Every deck wakes from the golden
  snapshot with the same MAC, address and player number. So it wakes with its
  cable out, takes its own MAC, waits for Mixxx to let go of the golden
  deck's address, and only then is plugged in: nothing on the link ever hears
  the clone. A booted deck (`--boot`, `--from`) has its own MAC from the start,
  through the device tree, and after every reboot.
- **The link stays with the deck** through `stop` and `up`, until
  `up NAME --link OTHER` (or `--link none`) on a stopped deck.
- **Decks can start together** (`deck up a --link booth & deck up b --link booth &`).
  Restored decks take turns on a link: each is plugged in and claims its
  number only once the one before it has (`b: waiting for the deck before
  it...`). A Mixxx whose Pro DJ Link library is new enough also settles two
  players claiming one number at the same moment by address (lib/prolink).
- **A deck that comes up unbrowsable gets its Mixxx restarted once.** An
  observer (a player number outside 1-4, as 7) can be neither browsed nor
  browse; a Pro DJ Link library from before its rebind fix could end up one
  when its address changed. `up` says so when it happens.
- `deck link NAME` says what the deck announces and who else is on its
  link. `deck link NAME unplug` pulls its cable (frames stop, and the Pi sees
  its link go down), and `plug` puts it back.

From the Mac, without touching any deck:

```sh
pi-qemu link list                       # every link, and the decks on it
pi-qemu link devices booth              # the players announcing themselves: number, name, address, MAC, deck
pi-qemu link capture booth booth.pcap   # every frame into a pcap until ^C, for Wireshark or `prolink pcap`
```

**How it works** (`app/src/board/link.*`). A link is a directory,
`~/.pi-qemu/links/NET/`, with one Unix datagram socket per deck on it,
`NAME.sock`. Each deck's QEMU gets one end of a socketpair as GENET's network
(`-netdev dgram,local.type=fd`); the deck's `pi-qemu run` holds the other end
and forwards: the frames its Pi sends go to the other decks' sockets, and
theirs to its Pi. It learns which deck has which MAC, as a switch does, and
floods broadcasts and unknown MACs. A deck never hears its own frames back,
and nothing leaves the Mac: the sockets are files, not ports. QEMU's own
multicast hub (`-netdev dgram` to a group) cannot send on macOS: it binds the
group's address, which BSD refuses to send from.

## Emulated CDJs

A CDJ-2000NXS on Pioneer's own firmware (1.44), emulated by the
`cdj2000-emulator` submodule and joining the decks' links. ProLink can then
be debugged without the booth: a TriMixxx deck with "real" CDJs, and the
CDJs with each other. `docs/cdj-emulator.md` says why it is built this way.

Once per Mac, and once per checkout:

```sh
pi-qemu cdj build                              # this checkout's emulator: ~1.5 min the first time
pi-qemu cdj firmware ~/Downloads/C2KNXS.UPD    # Pioneer's updater, extracted outside every checkout
```

- **The firmware** is Pioneer's, given to the NXS's owners: never in a
  repository. `firmware` checks every CRC and checksum, and writes what the
  emulator boots into `~/.pi-qemu/cdj/firmware/nxs/`.
- **`build`** fetches QEMU once, at the revision the emulator's patches
  take, into `~/.pi-qemu/cdj/`, and builds it into the submodule's ignored
  `build/`. It also builds the GUI core and a venv for the emulator's tools.
  In a worktree, the submodule is the main checkout's, which
  `pi-qemu worktree prepare` borrows.

A CDJ, and a booth:

```sh
pi-qemu deck up d --link booth
pi-qemu cdj up a --link booth          # ~25 s: "a: player 1 at 169.254.x.y, 02:43:44:.. on link booth"
pi-qemu cdj up b --link booth          # player 2: the NXS numbers itself AUTO
pi-qemu link devices booth 10          # 1 a, 2 b, 4 d
pi-qemu cdj shot a a.png               # its 480x234 screen
pi-qemu cdj press a link               # a panel key: play, cue, link, usb, sd, enter, back, menu, ...
pi-qemu cdj rotary a 2                 # the browse encoder
pi-qemu cdj window a                   # its screen and panel, lit as its firmware lights it
pi-qemu cdj lamps a                    # those lamps, by name: {"PLAY_PAUSE": "blink", "MASTER": "3", ...}
pi-qemu cdj status a                   # the emulator's own: the browser's last reply, the frame, ...
pi-qemu cdj rm a
```

- **A CDJ is booted, never restored.** Its player screen is up in about
  8 s, and `up` waits for it, then on a link for its first keep-alive. One
  with no player screen in 120 s is stopped, its logs left in
  `~/.pi-qemu/cdj/NAME/` until `cdj rm`.
- **On a link it is a member of its own,** as a deck is. Its MAC comes from
  its name, `02:43:44:..`, and the NXS takes its link-local address from
  the MAC's last two bytes.
- **Give `link devices` 10 s with a CDJ on the link.** A CDJ re-claims its
  number every few seconds with a TriMixxx deck present, as a real one did
  in `mixxx/lib/prolink/captures/S10-serve-to-cdj`, so its keep-alives come
  up to ~5 s apart.
- **Media:** `--sd IMG` / `--usb IMG` mount a FAT32 image, and its writes are
  thrown away. `--test-track` makes a card with a 10 s WAV on it. To make
  an image from a rekordbox stick, copy the stick to a folder first (never
  hand the emulator the stick itself: a CDJ writes its history to it), then
  `python -m tools.cdj_main.make_sd_image FOLDER IMG --size 4G` in
  `cdj2000-emulator/` with its venv (`.venv/bin/python`): a few seconds.
- **Playing a track: `--dsp-model`.** By default the CDJ runs Pioneer's DSP
  code, interpreted, which cannot decode in real time: a loaded track does
  not play. `--dsp-model` runs the emulator's behavioural DSP instead. It
  executes none of the DSP's code and makes no sound, and keeps the track's
  position on the CDJ's clock, so a track plays in real time and its beat
  packets come at its tempo.
- **Its window** (`cdj window NAME`, or `cdj up NAME --window`): its
  screen, live, in a schematic of the NXS's top panel, in the deck panel's
  look.
  - **Every key `cdj press` knows is a pad** in its place. A click holds it
    for as long as the mouse is down, and at least as long as a `press`.
    Shift-click latches it.
  - **The selector:** drag or scroll it to turn it, click it to push.
    Keyboard: space is PLAY, C CUE, up and down turn the selector, return
    pushes it, backspace is BACK.
  - **The TEMPO fader:** drag it; minus is at the top, as on the deck, and
    a double-click puts it back on its centre. The CDJ's own screen shows
    the tempo it took.
  - **The DIRECTION lever and the SD lid** are switches: a click flips them.
    They, and the fader, start as the CDJ has them.
  - **The pads light as the CDJ's firmware lights its lamps:** what MAIN
    sends the panel, which the emulator decodes (`cdj lamps`). The sources,
    the browse keys, SYNC, MASTER and LOOP MODE glow dimly while available.
  - **Not here:** the jog, which is not emulated (only its touch is).
  - **Closing it leaves the CDJ running.** The CDJ stopped, it closes, and
    it lets go of any key it held down.
  - **A key left down** (a window that crashed, or was killed with -9):
    open the window again and click the key once.
  - **A window and the command line share the CDJ:** the emulator's control
    channel takes several connections at once.
- **Its state** is in `~/.pi-qemu/cdj/NAME/`: `cdj.json`, the `cdj run`
  behind it (`pid`, `pi-qemu.log`), its socket on the link (`link.sock`),
  the emulator's run directory (`run/`, the last one kept as `run.prev`),
  and its window's `window.pid` and `window.log` while one is open.
- **Real time.** A CDJ runs at a real one's speed by the wall clock: its
  start-up, keep-alives (every 2.0 s) and status packets keep a real NXS's
  timing (`docs/cdj-emulator.md`, "Real time"). With every core of the Mac
  busy, its keep-alives slip to 2.1-3.2 s while that lasts.
- **Limits.** A CDJ takes about two cores (one with `--dsp-model`). Two
  CDJs and a deck take about five of a twelve-core Mac. There is no audio
  and no jog, and a CDJ stops after a day.

**How it joins a link** (`app/src/board/streamport.*`). `cdj up` starts
`pi-qemu cdj run` detached; it is the emulator's parent.
- It holds the CDJ's member on the link, through the same `Link` as a
  deck's.
- It listens on `link.sock` for the emulator's NIC: a QEMU stream netdev,
  `nxs_vm --link-hub unix:...`, whose frames are each a 32-bit length and
  then the frame.
- It moves them between that stream and `Link`'s datagram end.
- When the emulator exits, `cdj run` leaves the link.

## Deploying

`pi-qemu deck deploy TARGET [STEP...]` puts this checkout's code onto a deck,
emulated or real, by the numbered steps in `deploy/`, all of them in order
when none are named:

| Step | What |
|---|---|
| `000_base.pi.sh` | pi_config/fresh-install.md 1–2: boot flags, ssh, background load, packages, autologin |
| `001_ttymidi.sh` | the serial<->MIDI bridge (`mixxx_config/ttymidi`), built in Docker |
| `002_system.sh` | `pi_config`: systemd units, session, splash, eth0, the Wi-Fi fallback, dj-usb |
| `003_launcher.sh` | `trimixxx-launcher`: boot modes, SysEx daemon, deck keys, built in Docker |
| `004_mixxx.sh` | the Mixxx fork, built in Docker, over apt's binary |
| `005_config.sh` | `mixxx_config`: mapping, skin, fonts, mixxx.cfg, this deck's wiring |
| `006_library.sh` | Mixxx's music library directory, once |
| `007_doom.sh` | Doom |

A step is named as `mixxx`, `004` or `004_mixxx`, and a new step is the next
number. `deploy/lib.sh` has their contract:
- a `.sh` step runs on the Mac and reaches the deck as `ssh deck`; a `.pi.sh`
  step runs on the deck, as root;
- each is idempotent: run again, it changes nothing where nothing changed
  (a binary that is the same is left alone);
- its exit status says what follows: a step that changed the boot flags has
  the deck reboot before the next one; one that restarted the MIDI bridge or
  the launcher has the session restarted at the end, since Mixxx opens their
  ports only at its start. After a session restart, `deploy` waits until
  Mixxx is ready.

Steps that build in Docker need ~6 GB free on its disk: `deploy` checks
first, and leaves freeing space to you.

## Cards and releases

`pi-qemu image build [DECK]` makes a deck's card from the stock image
(`image/stock.env`):
1. seeds cloud-init: user, key, password hash, hostname (`image/user-data.in`,
   `network-config`);
2. boots it as an emulated deck of its own, `build-DECK` (`deck ssh build-DECK`
   reaches it while it builds);
3. runs every deploy step over it;
4. seals it into `.cache/build/DECK.img`, and for trimixxx0 refreshes the
   golden snapshot.

Everything up to and including `000_base` is cached in `.cache/base/` while
its inputs stay the same (`--rebuild-base` forces it).

The cards the decks run from, read-only and updated A/B over the network,
come from such a build (PLAN.md Part 1, and [rauc-pi-4-setup.md](rauc-pi-4-setup.md)):

```sh
pi-qemu release build                # HEAD's pi/vX.Y.Z tag -> release/out/X.Y.Z/: the card, the bundle
pi-qemu release card trimixxx2       # trimixxx2's card: the release card with its identity on p7
pi-qemu deck ship --host trimixxx-pi-2   # the update onto a deck running a release card
pi-qemu deck prepare --host trimixxx-pi-2 trimixxx2   # before a deck's first release card
```

## Git worktrees

Several agents (or people) can work at once, each in a git worktree of the
repo, each deploying its own code to its own emulated deck. The root
`CLAUDE.md` is the workflow agents follow. Behind it:

- **`pi-qemu worktree prepare`** sets up the worktree's submodules, which
  start as empty directories. `mixxx/` (and its `lib/prolink`) and
  `mixxx_config/ttymidi` become linked worktrees of your main checkout's
  submodule repositories, detached at the commits the worktree's branch
  records. So does `cdj2000-emulator/`, the emulated CDJs', when the branch
  records it and the main checkout has it
  (`git submodule update --init cdj2000-emulator` there gives it one). It takes seconds and downloads nothing. A branch made in a
  worktree's `mixxx/` is a branch of your main `mixxx/` at once, so merging it
  there is enough. `git submodule update` would instead clone a private copy
  that is deleted with the worktree.
- **Every checkout builds Mixxx in its own Docker build tree** (keyed by
  `mixxx/checkout-id.sh`). A tree builds from its own copy of the sources,
  which `mixxx/tree-sync.sh` updates by content before each build. ninja then
  recompiles exactly what changed since that tree's last build, whatever git
  did to the files' dates. A worktree's first build starts from a copy of the
  main checkout's tree.
  - A worktree's first Mixxx deploy takes about 75 s, later ones about 45 s.
  - Builds queue on the shared compiler cache, so only one Mixxx compiles at a
    time: two agents deploying at once measured 87 s and 138 s.
  - Each tree takes about 4 GB of Docker's disk.
- **`pi-qemu worktree release`**, before removing a worktree, empties its
  Docker build trees and removes its submodule worktrees. Until then git
  refuses to remove a worktree with checked-out submodules, short of
  `--force`, which leaves the build trees behind. `release` refuses itself if
  they hold uncommitted changes, or commits no branch has.
  - A submodule that is a clone of its own, as `git submodule update` makes,
    is taken back too, once its origin has all of its commits and it holds
    no stash.
  - Its git directory is in the worktree's `modules/`, which `release`
    removes, since git will not remove a worktree with it.
  - `release` also refuses while a CDJ runs from the worktree's emulator:
    `cdj rm` it first.
  `pi-qemu worktree status` shows what a worktree has checked out.

pi-qemu acts on the checkout it runs in: `deck deploy` from a worktree
deploys that worktree's code, and runs `prepare` itself. The built QEMU, the
caches, the golden snapshot and the releases are the main checkout's,
whichever checkout pi-qemu runs in.

## Snapshots

`pi-qemu deck save NAME FILE` pauses the Pi, writes the whole machine (RAM and
every device) to FILE, and stops it. `pi-qemu run --restore FILE CARD` brings
that machine back in about 3 seconds, Mixxx already running. Rules:

- **The card must be the one it was saved with, unchanged.** After the
  restore, the card moves on and the snapshot no longer fits it.
  `deck up` and `deck stop` do this for you: a saved machine is used once,
  and is gone the moment it starts.
- **The same devices.** No USB sticks inserted when saving (`save` refuses),
  and the same `--net` mode. `--audio` and `--display` may differ.
- **Same Mac architecture.** Some devices are saved in host byte order.
- The Pi wakes up with the clock it was saved with. `deck up` sets it to
  now; by hand, use `ssh ... "sudo date -u -s @$(date -u +%s)"`.

The **golden snapshot** (`.cache/golden/trimixxx0.img` + `.state`) is the
built card, booted until Mixxx has its sound device open, then saved. Every
new emulated deck starts from it. `image build trimixxx0` refreshes it at the
end of the build; `deck golden [CARD]` makes it from any card.

Making QEMU's `raspi4b` savable took patches 8–13 in
`qemu/trimixxx-patches.py`. The USB sound card and USB network adapter had no
saved state. The PCIe root port and GENET silently had none, so a restored Pi
lost its USB controller. USB devices forgot their configuration, and xHCI
without MSI-X could not be loaded. Each patch says why.

Patches 14–16 made the emulated SD card fast. It moved one byte per call
through the card model and one 512-byte block per image access, which gave
about 4 MB/s. It now reads at ~800 MB/s and writes at ~400 MB/s, which is
what took a boot from ~50 s to ~5 s. No real card is that fast. The emulator
is for testing the software, not the deck's timing.

## Workflows

### Test a Mixxx change on the emulated deck

```sh
pi-qemu deck up mix
pi-qemu deck deploy mix mixxx         # Docker arm64 build, swaps /usr/bin/mixxx, waits for the new Mixxx
pi-qemu deck ssh mix 'tail -50 /tmp/mixxx/mixxx.log'
pi-qemu deck shot mix /tmp/after.png
```

The build goes by content. Every checkout has its own build tree in Docker's
cache, with its own copy of the sources, and recompiles exactly the files
whose content changed since that tree's last build. `tree-sync: N change(s)`
in the output says how many. A changed file or two deploys in about 45 s.
The main checkout's first build in this layout compiles everything (~25 min
at the 4 jobs Docker's memory allows); after that, builds only redo what
changed.

### Test a config, skin or mapping change

```sh
pi-qemu deck deploy mix config        # mapping, skin, fonts, mixxx.cfg; ~10 s with the restart
pi-qemu deck press mix hotcue1
pi-qemu deck leds mix
pi-qemu deck ssh mix 'sudo grep ^0: /proc/tty/driver/ttyAMA'   # rx: bytes the S3 sent
```

`deploy mix system` (pi_config: systemd units, session, splash),
`launcher` and `ttymidi` work the same way, on a real deck too
(`--host ALIAS`).

### Play a track from a USB stick

```sh
pi-qemu deck stick mix list          # images in .cache/sticks/ and the Mac's USB disks
pi-qemu deck stick mix insert SAM3   # SAM3.img: a rekordbox export, read-only
pi-qemu deck press mix push          # the encoder push opens the library...
pi-qemu deck browse mix 2; pi-qemu deck press mix push   # ...then moves and activates
pi-qemu deck shot mix pick.png       # see where you are
pi-qemu deck press mix play; pi-qemu deck leds mix       # "play": true
```

An image of a real stick, with the same layout as the original (MBR, FAT32),
as `.cache/sticks/SAM3.img` was made:

```sh
cd pi-qemu/.cache/sticks
hdiutil create -srcfolder /Volumes/SAM3 -volname SAM3 -fs FAT32 -layout MBRSPUD -format UDTO -size 256m SAM3
mv SAM3.cdr SAM3.img
```

### A folder as a stick, with nothing copied

A folder on the Mac goes in as a FAT32 stick made up on the fly: a copy of
a DJ's stick on an external drive, say, however big. Nothing is copied and
the folder is only ever read. pi-qemu builds the stick's directories and FATs
in memory when it goes in, then hands the Pi each file's bytes as it reads
them, through QEMU's NBD client (`app/src/board/fatvolume.*`, `nbdserver.*`).
The Pi sees a real USB disk, so udev, dj-usb and Mixxx's own checks all run
as they would on the deck.

```sh
pi-qemu deck stick mix insert /Volumes/Drive/DJ-stick-copy   # a folder: MBR, FAT32, sized to fit
pi-qemu deck stick mix insert SANDISK                        # .cache/sticks/SANDISK.stick
```

A `.stick` file (JSON) says how the stick was laid out. Every key but
`folder` is optional, and relative paths are from the `.stick` file:

```json
{
  "folder": "/Volumes/BIGBOY2/TriMixxx-USB-dumps/SanDisk-E02C-5D1B/files",
  "superfloppy": true,
  "size": 123048296448,
  "volumeStart": "/Volumes/BIGBOY2/TriMixxx-USB-dumps/SanDisk-E02C-5D1B/sda-first-31MiB-bootsector-and-FATs.raw",
  "bytesPerSecond": 3700000
}
```

| Key | What it does |
|---|---|
| `superfloppy` | no partition table: the filesystem starts at sector 0, as some sticks have it |
| `partitionStart` | where the FAT32 partition starts, in sectors (default 2048) |
| `size` | the disk's size in bytes (default: what the files need, plus a quarter) |
| `volumeStart` | a raw copy of a real stick's filesystem start (`{"file": F, "offset": N}`, or just `F`): its reserved sectors are served as they were (boot code, serial, label, dirty flag), and the FATs, FSInfo and directories are laid out to match |
| `mbr` | a real stick's first sector, partition table included |
| `label`, `serial` | the volume label, and the serial blkid reports as the UUID (`"E02C-5D1B"`) |
| `clusterKiB` | the cluster size (default: mkfs.fat's for the size) |
| `bytesPerSecond` | reads no faster than this, like a slow stick (a USB 2 Cruzer Blade: `3700000`); see "A slow stick" |
| `readsPerSecond` | and no more reads a second than this, small or large (what makes a cheap stick's small reads slow too) |

Names go onto the stick NFC, as rekordbox's sticks have them: macOS hands a
folder's accented names back decomposed, and serving those as they come would
make a stick no deck has ever seen. A file of 4 GiB or more, or two names that
differ only in case, cannot be on a FAT32 stick, and the insert says so.

### A slow stick, and what the deck reads off it

Every stick reads through a QEMU throttle group of its own, so any stick --
a folder, an image, the Mac's own -- can be made as slow as a real one, and
its speed changed while it is in:

```sh
pi-qemu deck stick mix speed SANDISK 4M        # 4 MB/s, a cheap USB 2 stick
pi-qemu deck stick mix speed SANDISK 4M/300    # ...and 300 reads a second
pi-qemu deck stick mix speed SANDISK full      # as fast as it goes
pi-qemu deck stick mix list                    # an inserted stick shows its speed
```

A speed set before the stick goes in applies from its first read; one set
while it is in takes effect at once (QMP `qom-set` on its group's `limits`).
Sticks really do vary that much: a Cruzer Blade gave 3.7 MB/s, a SanDisk 3.2
on USB 3 well over 100.

For a folder stick, pi-qemu also counts what the Pi reads, file by file:
what a browse reads, what a load reads, and what was read that nobody asked
for:

```sh
pi-qemu deck stick mix reads SANDISK           # since it went in, or the last reset
pi-qemu deck stick mix reads SANDISK reset     # the same, then start counting again
```

```text
139.3 MB in 3612 reads over 46.4 s (3.0 MB/s on average), from 1102 files and regions
   80.4 MB     643 reads  /Contents/Zoe/UnknownAlbum/Zoe - Transient Shift.wav
   58.3 MB     466 reads  /Contents/FAÏG/South West EP/FAÏG - South West EP - 03 Deep Within.aiff
    0.3 MB     598 reads  FAT
```

### Check the audio without hearing it

```sh
pi-qemu deck record mix 10 /tmp/mix.wav    # Mixxx's main mix, recorded by Mixxx; make it play first
pi-qemu deck up snd -- --audio wav:/tmp/out.wav    # or everything the sound card plays, for a new deck
python3 - <<'EOF'
import wave, struct
w = wave.open('/tmp/out.wav'); n = w.getnframes(); d = w.readframes(n)
s = struct.unpack('<%dh' % (n * w.getnchannels()), d)
print('seconds', n / w.getframerate(), 'peak', max(map(abs, s)))
EOF
```

A tone that does not need Mixxx: stop the session with
`sudo systemctl stop getty@tty1` (that also frees the sound card), then run
`timeout 3 speaker-test -D plughw:0,0 -c 1 -r 44100 -t sine -f 440`. That is
one tone on both channels; `-c 2` plays left, then right. To bring Mixxx back:
`sudo systemctl start getty@tty1`.

### The deck will not come up

```sh
cat ~/.pi-qemu/instances/NAME/card.run/console.log    # the boot, from the first kernel line
pi-qemu deck console NAME 'systemctl --failed' 'journalctl -b -p err | tail'
pi-qemu deck shot NAME now.png
```

If ssh hangs as soon as QEMU starts, a Mac firewall (Little Snitch, or macOS's
own with stealth mode) may be holding QEMU's network sockets. Allow
`qemu-system-aarch64`.

### Rebuild the card after changing the deck's setup

```sh
pi-qemu image build                   # reuses the cached base; refreshes the golden snapshot
```

Running decks keep their own cards; `deck up NAME --fresh` takes the new one.

## Troubleshooting

- **`... has a passphrase and is not in the ssh agent`**: `ssh-add` the key
  pi-qemu names (after every reboot of the Mac).
- **`QLocalServer::listen: Name error`**: a unix socket path longer than 104
  bytes. Use a shorter card path; instances live in `~/.pi-qemu` for this reason.
- **The window does not take your keys**: click into it. A window opened from a
  background process may not get focus.
- **Docker "no space left"**: check with `docker run --rm --pull never debian:trixie df -h /`.
  `docker buildx prune -f --all --filter type=regular --filter until=90m` frees
  space and keeps the compile caches.
- **A restore fails with "Unknown section" or similar**: the snapshot was made
  by a QEMU built from different patches. Remake it with `pi-qemu deck golden`.
- **`deploy` says Docker's disk is full**: see `docker system df`. Unused
  images (`docker image prune -a`) are usually most of it.
