# pi-qemu: a TriMixxx deck on your Mac

`pi-qemu` runs a deck's SD card on an emulated Raspberry Pi 4 (QEMU's
`raspi4b`, accelerated by Apple's Hypervisor framework). It boots the real
Raspberry Pi OS card with the real deck software: Mixxx, ttymidi, the launcher
and the panel session. You get the Pi's 1280x800 screen in a window (click and
drag like the touchscreen), and a control panel standing in for the deck's S3
board. The panel sends MIDI to the Pi's UART exactly as the real S3 does.
Everything the panel does can also be done from the command line, so scripts
and AI agents can drive a deck too.

The emulated deck is called **trimixxx0**: a virtual trimixxx2, wired by
`mixxx_config/units/trimixxx0.json`. Why things are built the way they are is
in [PLAN.md](PLAN.md).

## What is in here

| Path | What it is |
|---|---|
| `app/` | `pi-qemu` itself (C++/Qt): the firmware step, QEMU, the S3, the panel, the CLI |
| `app/icons/` | its icon: `trimixxx.svg`, and the `.ico` compiled in, made from it by `make-ico.py` |
| `qemu/build.sh`, `qemu/trimixxx-patches.py` | builds the pinned, patched QEMU |
| `image/build.sh` | builds a deck's card from the stock Raspberry Pi OS image |
| `deploy.sh`, `deploy/base.sh` | sets up or updates any deck over ssh, real or emulated |
| `instance.sh` | runs emulated decks side by side (agents, tests), from snapshots |
| `worktree.sh` | readies a git worktree of the repo to build and deploy the deck's code |
| `console.py` | runs commands on a deck over its serial console, without ssh |
| `.cache/` (gitignored) | images, built cards, the golden snapshot, USB stick images |

## Setting up a laptop

On an Apple Silicon Mac:

```sh
brew install qemu mtools cmake ninja qt python@3.12 dtc   # QEMU's deps, mtools for FAT, Qt for the app
pi-qemu/qemu/build.sh        # ~10 min the first time: QEMU 11.1.0 + patches, and dtmerge
pi-qemu/app/build.sh         # pi-qemu -> pi-qemu/app/build/pi-qemu
cp pi-qemu/image/secrets.example.env pi-qemu/image/secrets.env   # then set SAM1902_PASSWORD
```

You also need Docker Desktop running (the deck's arm64 binaries are Docker
builds) and an ssh key at `~/.ssh/no_pass/rsa_sam`, or `SSH_KEY=` pointing
at another one. The key gets into the card at build time.

Then build the card. Most of the time goes to apt and the Docker builds of the
deck's binaries; the base stage is cached for later runs:

```sh
pi-qemu/image/build.sh            # -> pi-qemu/.cache/build/trimixxx0.img (+ the golden snapshot)
```

A window shows the build as it goes: `pi-qemu build-log pi-qemu/.cache/build/build.log`.

## Running your deck

```sh
pi-qemu/app/build/pi-qemu run pi-qemu/.cache/build/trimixxx0.img
```

Two windows open: the Pi's screen and the control panel. Log in on the
console as `sam1902` with the password from `image/secrets.env`, or use ssh:

```sh
ssh -p 2222 -i ~/.ssh/no_pass/rsa_sam sam1902@127.0.0.1
```

**Sound is off unless you ask for it.** `--audio speakers` plays the deck's
output on the Mac. `--audio wav:/abs/path/out.wav` records it to a file
instead, which is how to check audio without hearing it.

Useful options (`pi-qemu run --help` lists them all):

| Option | |
|---|---|
| `--audio none\|speakers\|wav:FILE` | the deck's sound output (default none: silent) |
| `--display window\|vnc\|none` | the Pi's screen; screenshots work in every mode |
| `--no-controls` | no panel window: the command line only |
| `--ssh PORT` | ssh port on 127.0.0.1 (default 2222) |
| `--stick FILE.img` | a USB stick image plugged in from power-on |
| `--deck NAME` | whose wiring the panel follows (`mixxx_config/units/NAME.json`) |
| `--restore FILE` | start from a saved machine instead of booting (see Snapshots) |
| `--private` | a deck of its own, beside yours: see "Several decks at once" |

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
  - every USB storage device on the Mac and every image in `.cache/sticks/`,
    each with Insert / Unplug (two slots, read-only).

### Driving it from the command line

While a deck runs, `pi-qemu COMMAND` talks to it (`pi-qemu help` lists them).
For a deck started with `instance.sh`, the same commands are
`pi-qemu/instance.sh ctl NAME COMMAND`:

```sh
pi-qemu press play              # press and release (80 ms; `press cue 500` holds it)
pi-qemu down cue; pi-qemu up cue
pi-qemu jog 3240                # a quarter turn clockwise (12960 ticks per turn)
pi-qemu touch on                # the platter's touch sensor, for scratching
pi-qemu browse -3               # the track encoder; + is up
pi-qemu tempo 8192              # the fader, 0..16383; 8192 is the centre
pi-qemu midi 90 3C 7F           # raw bytes on the UART
pi-qemu leds                    # what the deck's lights show now, as JSON
pi-qemu stick list | stick insert SAM3 | stick unplug SAM3
pi-qemu status | power on | power off | screenshot shot.png
pi-qemu save FILE               # the whole machine to FILE, then off
```

Control names: `tempo-range keylock loop8 loop4 loop-double loop-halve back
hotcue1..4 slip sort play cue loop-in loop-out reloop push jog-touch`. They
follow the deck's wiring, so `press hotcue1` sends whatever note trimixxx0's
hot cue 1 is on.

The command finds your deck through `~/.pi-qemu/current`, a link to the last
plain (non-`--private`) deck started. `PI_QEMU_CONTROL=<run dir>/control.sock`
points it at any other deck.

### Where things are

A deck's run directory is next to its card (`trimixxx0.img` gets
`trimixxx0.run/`). It holds:
- `qemu.cmd`: the exact QEMU command, to rerun by hand;
- `qemu.log`;
- `console.log`: the Pi's serial console, from the first kernel line;
- `ssh.port`;
- the sockets `control.sock` (pi-qemu's CLI), `console.sock` (the Pi's console:
  `nc -U console.sock`), `qmp.sock` and `qmp-fd.sock` (QEMU's monitor) and
  `s3.sock` (the S3's UART).

Inside the Pi:
- Mixxx logs to `/tmp/mixxx/mixxx.log` and `/tmp/mixxx/stderr.log`; the
  previous run's are kept as `.1`;
- its settings are in `~/.mixxx/`;
- the launcher's log is `journalctl -u trimixxx-launchd`.

## Several decks at once: `instance.sh`

`instance.sh` runs decks side by side, for agents and tests. Each one has its
own copy of the card, ssh port, ssh alias and control socket. None of them
touches your own deck, port 2222 or `~/.pi-qemu/current`. They are headless and
silent unless asked.

```sh
pi-qemu/instance.sh up mine                 # restored from the golden snapshot in ~4 s
pi-qemu/instance.sh ssh mine 'pgrep -a mixxx'
pi-qemu/instance.sh ctl mine press play     # any pi-qemu command, for this deck
pi-qemu/instance.sh shot mine /tmp/shot.png # its screen, with no window
pi-qemu/instance.sh deploy mine config      # this checkout's deploy.sh onto it; waits for the new Mixxx
pi-qemu/instance.sh run mine -- pi_config/upload.sh   # any script that uses HOST + plain ssh/scp
pi-qemu/instance.sh ready mine              # wait until Mixxx plays, its controller open
pi-qemu/instance.sh console mine 'ip -br a' # over the serial console when ssh is down
pi-qemu/instance.sh stop mine               # suspend: saved, next `up` ~3 s
pi-qemu/instance.sh rm mine                 # gone, card and all
pi-qemu/instance.sh list
```

`deploy` and `run` set `HOST` and put the instance's own `ssh`/`scp` first in
`PATH`, inside one process. That is the safe way to point the repo's upload
scripts at an instance: on their own they default to `HOST=trimixxx-pi`, a
real deck, and in zsh `$HOST` is the Mac's own name. (`instance.sh env NAME`
prints the same exports for a shell of your own.)

An instance lives in `~/.pi-qemu/instances/NAME/`. That path is kept short on
purpose, because macOS caps unix socket paths at 104 bytes. `up --window`
shows its screen and panel. `up --boot` boots it instead of restoring it
(~5 s to ssh, Mixxx playing ~1 s later).
`up --fresh` starts again from the golden snapshot, and `up --from CARD`
boots a card of your own. The script's header documents the rest. Every
command prints or says what it runs, so you can do any of it by hand.

Each instance takes 2 GB of RAM; four at once is about the limit on a 16 GB Mac.

## Git worktrees

Several agents (or people) can work at once, each in a git worktree of the
repo, each deploying its own code to its own emulated deck. The root
`CLAUDE.md` is the workflow agents follow. Behind it:

- **`pi-qemu/worktree.sh prepare`** sets up the worktree's submodules, which
  start as empty directories. `mixxx/` (and its `lib/prolink`) and
  `mixxx_config/ttymidi` become linked worktrees of your main checkout's
  submodule repositories, detached at the commits the worktree's branch
  records. It takes seconds and downloads nothing. A branch made in a worktree's
  `mixxx/` is a branch of your main `mixxx/` at once, so merging it there is
  enough. `git submodule update` would instead clone a private copy that is
  deleted with the worktree.
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
- **`pi-qemu/worktree.sh release`**, before removing a worktree, empties its
  Docker build trees and removes its submodule worktrees. Until then git
  refuses to remove a worktree with checked-out submodules, short of `--force`,
  which leaves the build trees behind. `release` refuses itself if they hold
  uncommitted changes, or commits no branch has.
  `pi-qemu/worktree.sh status` shows what a worktree has checked out.

`instance.sh deploy` from a worktree runs `prepare` itself when a step needs
the submodules, and always deploys the code of the checkout its `instance.sh`
is in.

## Snapshots

`pi-qemu save FILE` pauses the Pi, writes the whole machine (RAM and every
device) to FILE, and stops it. `pi-qemu run --restore FILE CARD` brings that
machine back in about 3 seconds, Mixxx already running, where a boot takes
about 5 and Mixxx's start a little more. Rules:

- **The card must be the one it was saved with, unchanged.** Restore a card
  copy taken at save time. After the restore, the card moves on and the
  snapshot no longer fits it. `instance.sh` does this for you: clone both,
  restore, delete the snapshot.
- **The same devices.** No USB sticks inserted when saving (`save` refuses),
  and the same `--net` mode. `--audio` and `--display` may differ.
- **Same Mac architecture.** Some devices are saved in host byte order.
- The Pi wakes up with the clock it was saved with. `instance.sh` sets it to
  now; by hand, use `ssh ... "sudo date -u -s @$(date -u +%s)"`.

The **golden snapshot** (`.cache/golden/trimixxx0.img` + `.state`) is the
built card, booted until Mixxx has its sound device open, then saved. Every
new instance starts from it. `image/build.sh` refreshes it at the end of a
trimixxx0 build; `instance.sh golden [CARD]` makes it from any card.

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

## Building cards and deploying

`deploy.sh HOST [STEP...]` sets up or updates a deck over ssh. It works the
same against a real deck (`trimixxx2`) and an emulated one, and it is
idempotent. It never reboots, and says when a reboot is needed. Its steps:
- `base`: fresh-install.md 1–2;
- `ttymidi`;
- `system`: `pi_config/upload.sh`;
- `launcher`;
- `mixxx`: the fork, built in Docker;
- `config`: `mixxx_config/upload.sh`;
- `library`;
- `doom`.

`image/build.sh [HOSTNAME]` makes a card from the stock image:
1. seeds cloud-init: user, key, password hash, hostname;
2. boots the card in pi-qemu;
3. runs `deploy.sh` over it, step by step;
4. seals it, and for trimixxx0 refreshes the golden snapshot.

Everything up to and including `base` is cached in `.cache/base/` while its
inputs stay the same (`REBUILD_BASE=1` forces it). A real deck could be
flashed from a card built for its hostname. PLAN.md lists what is still
missing for that: per-card identity, root growing to fill the SD card, home
Wi-Fi, and the A/B layout for network updates.

## Workflows

### Test a Mixxx change on the emulated deck

```sh
pi-qemu/instance.sh up mix
pi-qemu/instance.sh deploy mix mixxx        # Docker arm64 build, swaps /usr/bin/mixxx, waits for the new Mixxx
pi-qemu/instance.sh ssh mix 'tail -50 /tmp/mixxx/mixxx.log'
pi-qemu/instance.sh shot mix /tmp/after.png
```

For your own deck (port 2222), give it an alias in `~/.ssh/config`:

```
Host trimixxx0-local
  HostName 127.0.0.1
  Port 2222
  User sam1902
  IdentityFile ~/.ssh/no_pass/rsa_sam
  StrictHostKeyChecking no
  UserKnownHostsFile /dev/null
```

Then `pi-qemu/deploy.sh trimixxx0-local mixxx` deploys to it.

The build goes by content. Every checkout has its own build tree in Docker's
cache, with its own copy of the sources, and recompiles exactly the files
whose content changed since that tree's last build. `tree-sync: N change(s)`
in the output says how many. A changed file or two deploys in about 45 s.
The main checkout's first build in this layout compiles everything (~25 min
at the 4 jobs Docker's memory allows); after that, builds only redo what
changed.

### Test a config, skin or mapping change

```sh
pi-qemu/instance.sh deploy mix config       # mapping, skin, fonts, mixxx.cfg; ~10 s with the restart
pi-qemu/instance.sh ctl mix press hotcue1
pi-qemu/instance.sh ctl mix leds
pi-qemu/instance.sh ssh mix 'sudo grep ^0: /proc/tty/driver/ttyAMA'   # rx: bytes the S3 sent
```

`deploy mix system` (pi_config: systemd units, session, splash),
`launcher` and `ttymidi` work the same way. `system` and `ttymidi` restart the
serial-to-MIDI bridge. Mixxx only opens the S3's port when it starts, so
`deploy.sh` then restarts the session, on a real deck too.

### Play a track from a USB stick

```sh
pi-qemu stick list                          # images in .cache/sticks/ and the Mac's USB disks
pi-qemu stick insert SAM3                   # SAM3.img: a rekordbox export, read-only
pi-qemu press push                          # the encoder push opens the library...
pi-qemu browse 2; pi-qemu press push        # ...then moves and activates: enter the stick, a list, load a track
pi-qemu shot pick.png                       # see where you are
pi-qemu press play; pi-qemu leds            # "play": true
```

An image of a real stick, with the same layout as the original (MBR, FAT32),
as `.cache/sticks/SAM3.img` was made:

```sh
cd pi-qemu/.cache/sticks
hdiutil create -srcfolder /Volumes/SAM3 -volname SAM3 -fs FAT32 -layout MBRSPUD -format UDTO -size 256m SAM3
mv SAM3.cdr SAM3.img
```

### Check the audio without hearing it

```sh
pi-qemu/instance.sh up snd -- --audio wav:/tmp/out.wav    # a new instance: rm an old one first
# ... play something in the deck; the file is a valid WAV at any moment
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
pi-qemu/instance.sh console NAME 'systemctl --failed' 'journalctl -b -p err | tail'
pi-qemu/instance.sh shot NAME now.png
```

If ssh hangs as soon as QEMU starts, a Mac firewall (Little Snitch, or macOS's
own with stealth mode) may be holding QEMU's network sockets. Allow
`qemu-system-aarch64`.

### Rebuild the card after changing the deck's setup

```sh
pi-qemu/image/build.sh                      # reuses the cached base; refreshes the golden snapshot
```

Running decks keep their own cards; `instance.sh up NAME --fresh` takes the new one.

## Troubleshooting

- **`port 2222 is already in use`**: another plain deck is running. Use
  `--ssh`, or `--private` for one that picks its own port.
- **`QLocalServer::listen: Name error`**: a unix socket path longer than 104
  bytes. Use a shorter card path; instances live in `~/.pi-qemu` for this reason.
- **The window does not take your keys**: click into it. A window opened from a
  background process may not get focus.
- **Docker "no space left"**: check with `docker run --rm --pull never debian:trixie df -h /`.
  `docker buildx prune -f --all --filter type=regular --filter until=90m` frees
  space and keeps the compile caches.
- **A restore fails with "Unknown section" or similar**: the snapshot was made
  by a QEMU built from different patches. Remake it with `instance.sh golden`.
- **`deploy` says Docker's disk is full**: see `docker system df`. Unused
  images (`docker image prune -a`) are usually most of it.
