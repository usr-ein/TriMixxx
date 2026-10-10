# Emulated CDJs

A CDJ-2000NXS running Pioneer's own firmware, emulated on the Mac, on the
same Pro DJ Link network as emulated TriMixxx decks. It lets ProLink be
debugged without the booth: two "real" CDJs with each other, and a CDJ with
a TriMixxx deck.

The emulator is [cdj2k-revival/cdj2000-emulator](https://github.com/cdj2k-revival/cdj2000-emulator)
(GPL-2.0-or-later; its fast GUI core GPL-3.0-or-later). It runs the CDJ's
two processors:
- **MAIN, an SH-4:** the player, the panel, the media and the Ethernet, on a
  QEMU machine written for it (`cdj2000nxs-main`).
- **GUI, a Blackfin BF531:** the 480x234 screen, on a Blackfin core.

The two talk over the serial link they use on the real board. The NXS's
audio DSP, a TI C674x, is interpreted. Audio is not modelled.

How to run one is in `pi-qemu/README.md`, "Emulated CDJs". This file says
why it is built this way: which emulator commits, where the firmware lives,
how a CDJ joins a link, and how changes go back upstream.

---

## How it works

### The emulator, and which commits

`cdj2000-emulator/` is a submodule on the fork
[usr-ein/cdj2000-emulator](https://github.com/usr-ein/cdj2000-emulator),
branch `main`. As in `usr-ein/mixxx`, the fork's `main` carries TriMixxx's
commits over upstream's. It is three things:

1. **Upstream main.**
2. **Our own changes,** each a commit made directly on upstream main, so
   each can go upstream alone ("Where the commits live").
3. **geepot's open PR series, #33 to #38,** merged over them. Its tip, #38's
   `pr2/launcher-defaults`, merges the whole series. Among them:
   - #35: SH-4 TCG fast paths
   - #36: the DSP on its own thread, and the exact DSP idle skip
   - #37: the fast Blackfin core, `cdj-gui-run`

The series is what makes ProLink possible.

On upstream main alone, the NXS boots to its player screen, but it is
starved: the DSP interpreter runs inside MAIN's own memory accesses. Its
Ethernet comes up and it sends DHCP discovers, but 38 s and 76 s apart. A
real NXS sends them 1 s and 2 s apart. In 240 s it never reached a Pro DJ
Link packet.

On the series, it runs the whole start-up as the real deck does
(`mixxx/lib/prolink/captures/S01-cold-boot-a`):
- three DHCP discovers
- ARP probes for its link-local address
- 3x hello, then claims of its MAC, IP and number, 3x each
- then keep-alives every 2.0 s

### Real time

A CDJ runs at the speed of a real one, by the wall clock: what the other
devices on a link see is a real NXS's timing. Measured on an M2 Pro,
against the real deck's cold boot in `S01-cold-boot-a`:

| | real NXS | emulated |
| --- | ---: | ---: |
| DHCP discovers, apart | 1.001, 2.003 s | 0.999, 2.000 s |
| first ARP probe after the last discover | 3.004 s | 3.001 s |
| ARP probes, apart | 1.001, 1.001 s | 1.000, 1.000 s |
| hello, claims, apart | 0.297-0.301 s | 0.297-0.301 s |
| first DHCP discover to first keep-alive | 12.6 s | 12.6 s |
| keep-alives, apart (10 min) | 2.003 s | 2.000 s |

With a second CDJ and a TriMixxx deck on the link, the CDJs keep the same
timing, and their status packets go out every 0.200 s, as a real NXS's do.
One of them keeps re-claiming its number while the deck is there, as the
real CDJ did in `S10-serve-to-cdj`, and its keep-alives then come 1.1-5.2 s
apart (2.6 s median, the real one's too).

It used to run at 0.64-0.74x (keep-alives every 2.7-3.1 s). Two things
slowed it, each a commit on the fork's `cdj-realtime` branch:
- **The RTOS lost a quarter of its ticks on macOS.** Its tick is a 1 ms
  timer, and QEMU's main loop on macOS waits in whole milliseconds: two
  expiries often ran in one pass, which the guest took for one. A
  high-resolution wait (a kqueue timer, as the Windows build already had a
  waitable timer) and a timer that keeps the underflows the guest has not
  seen yet fix it (`patches/qemu-main-loop-macos-hires.patch`,
  `qemu-sh-tmu-catch-up.patch`).
- **MAIN's clock stopped while it waited for the DSP.** On every HPI access
  MAIN waits for the DSP thread (~12K times a second, ~15 us each). Since
  geepot's series held the guest clock during those waits, MAIN's time ran
  slower than the wall clock. The waits are now MAIN's own time, and its
  clock is the wall clock (`CDJ_NXS_DSP_HOST_TIME`, on by default).

**Playing a track.** Pioneer's DSP code, interpreted, cannot decode in real
time on a Mac: with `--functional-dsp-audio` a track plays at 0.04-0.1x, and
real time needs ~90 M DSP packets a second where the interpreter does
~10 M. `pi-qemu cdj up --dsp-model` runs the emulator's behavioural DSP
instead: it runs none of the DSP's code and no audio, and keeps a loaded
track's position on the guest clock. A 180 BPM track from a rekordbox
stick played with beat packets every 0.33333 s on average (60/180), each
within 1% but one in sixty, and its status packets said it played.
Without `--dsp-model` a loaded track does not play.

### The firmware

Pioneer's, never in a repository, and nothing derived from it either: no
extracted images, flash copies, screenshots or run directories. Sam
downloads the NXS updater (`C2KNXS.UPD`, 1.44) himself;
`pi-qemu cdj firmware` splits and extracts it:

```sh
pi-qemu cdj firmware ~/Downloads/C2KNXS.UPD
```

- **Where it writes:** `~/.pi-qemu/cdj/firmware/nxs/`, outside every
  checkout. Every worktree shares it.
- **What it writes:** the MAIN flash image, the GUI boot ELF and the GUI
  flash, with the container's hashes.
- **How it extracts:** with the submodule's own extractors. They check every
  CRC and checksum before they write.
- **The ignore rules:** TriMixxx's `.gitignore` also ignores `*.UPD` and
  `*.upd`. The fork's `.gitignore` already ignores them, and `firmware/`,
  `runs/`, `*.bin`, `*.elf` and `*.img` with them.

### Building

```sh
pi-qemu cdj build
```

It builds this checkout's emulator, in the submodule's own (ignored)
`build/` and `bin/`:

- **QEMU** is pinned to `55347990`, the revision upstream tested. QEMU master
  breaks the SH-4 patches (upstream issue #41). The source is fetched once
  into `~/.pi-qemu/cdj/qemu-55347990/` and copied into each checkout with an
  APFS clone, which costs almost no disk.
- **Two scripts** do the build, upstream's `scripts/build-qemu-sh4.sh` and
  `scripts/build-cdj-gui-run.sh`.
- **The first build** in a checkout takes about 1.5 min. After that, only
  what changed is rebuilt.
- **Nothing to install:** it needs no Homebrew package beyond what pi-qemu
  already uses. QEMU's configure is handed a Python that can make venvs
  (uv's `~/.local/bin/python3` symlink cannot).
- **The submodule must be checked out first.**
  - In the main checkout, `cdj build` initialises it if it is missing.
  - In a worktree it never does: it borrows the main checkout's, through
    `pi-qemu worktree prepare` (see "Worktrees").
  - If neither is there, it fails and names the command to run.

### Worktrees

- **`pi-qemu worktree prepare` borrows `cdj2000-emulator`** from the main
  checkout, as it borrows `mixxx`: a linked worktree of the main checkout's
  clone, detached at the commit this branch records.
  - A main checkout without it is skipped, with the command that gives it
    one: `git -C MAIN submodule update --init cdj2000-emulator`.
- **`pi-qemu worktree release` takes it back,** in either of two shapes:
  - **Borrowed:** as `mixxx`. It refuses while the copy holds uncommitted
    work, or commits no branch has.
  - **A private clone,** which `git submodule update` makes, with its gitdir
    in the worktree's own `modules/`:
    - it refuses while the clone holds uncommitted work, a stash, or commits
      its origin lacks
    - otherwise it removes the clone's files, then the worktree's whole
      `modules/` directory; not `git submodule deinit`, which edits the
      `.git/config` the main checkout shares
  - git will not remove a worktree while its `modules/` exists, even empty,
    so `git worktree remove` (and `crew clean`) succeed only after this.
  - It also refuses while a CDJ runs from this checkout's emulator:
    `cdj rm` it first.
- **The emulator's build lives in the submodule's ignored `build/`,** so it
  goes with the worktree.

### A CDJ

```sh
pi-qemu cdj up a --link booth          # headless; --window shows its screen and panel
pi-qemu cdj list
pi-qemu cdj status a
pi-qemu cdj shot a a.png               # its 480x234 screen
pi-qemu cdj press a link               # a panel key: play, cue, link, usb, sd, enter, back, menu, ...
pi-qemu cdj rotary a 2                 # the browse encoder
pi-qemu cdj rm a
```

- **A CDJ is not a deck.** It has no snapshot, so `up` boots it every time.
  Its player screen comes up in about 8 s, and it is on the link in about
  25 s. `up` waits for its first keep-alive and says what it claimed:
  `a: player 1 at 169.254.155.21, 02:43:44:24:9b:15 on link booth`.
- **Its state** lives in `~/.pi-qemu/cdj/NAME/`: the run directory with its
  logs and screens, its socket on the link, and `cdj.json`.
- **Its processes:**
  - `pi-qemu cdj run`, which `up` starts detached, is `nxs_vm`'s parent.
  - When the emulator exits, `cdj run` leaves the link and exits too.
  - The series' parent watch stops QEMU and the GUI core if `nxs_vm` dies.
  - `cdj rm` stops the whole tree and deletes its state.
- **A CDJ on no link** has no cable in.
- **Media:**
  - `--sd IMG` or `--usb IMG` mounts a FAT32 image; guest writes go to an
    overlay that is thrown away.
  - `--test-track` makes a card with a 10 s WAV on it.
- **Keys:** `press` uses upstream's NXS key names, and `rotary` turns the
  browse encoder. Both go through upstream's `tools.cdj_main.dev`, which
  logs every action in the run directory.

### On a link

A CDJ joins a pi-qemu link (`deck up --link NET`) as a member of its own,
`~/.pi-qemu/links/NET/NAME.sock`, so TriMixxx decks and CDJs share one
network:

```sh
pi-qemu deck up d --link booth
pi-qemu cdj up a --link booth
pi-qemu cdj up b --link booth
pi-qemu link devices booth 10  # 1 a, 2 b (AUTO numbering), 4 d
pi-qemu link capture booth booth.pcap
```

- **How a CDJ's frames reach the link.**
  - The emulator speaks QEMU's framed stream netdev: a 32-bit big-endian
    length, then the frame. That is what its own `link_hub` uses.
  - `nxs_vm --link-hub unix:~/.pi-qemu/cdj/NAME/link.sock` puts the NXS's
    Ethernet on it.
  - `pi-qemu cdj run`, which `up` starts detached, holds the other end.
  - It moves frames between that stream and its link member, through
    pi-qemu's `Link`, unchanged: the same switch the decks use, with MAC
    learning and flooding.
  - The emulator knows nothing of pi-qemu.
- **Its MAC comes from NAME**, in a block of its own, `02:43:44:..`; decks
  use `02:54:4d:..`. pi-qemu writes it where MAIN reads it, a record in the
  flash sector at `0x3f8000` (`nxs_vm --link-mac`), into a run-local copy
  of the flash.
  - The NXS takes its link-local IP from the MAC's last two bytes:
    `..:67:ac` becomes `169.254.103.172` on a real deck.
  - So the fifth byte is kept within 1..254, outside the reserved
    `169.254.0/24` and `169.254.255/24`.
  - A blank record would give every CDJ `00:00:00:00:00:01` and
    `169.254.0.1`.
- **Frames shorter than 60 bytes are padded** by the emulator's NIC, as the
  wire pads them. Linux sends 42-byte ARP replies. The NIC used to count
  them as runts and drop them, so a CDJ never learned a deck's address and
  never sent it its status.

### Limits

- About two cores per CDJ (the TCG thread and the DSP thread; one with
  `--dsp-model`). Two CDJs and a deck take about five of the Mac's twelve.
- Real time needs the cores: with every core of the Mac busy, a CDJ's
  keep-alives slip to 2.1-3.2 s while it lasts.
- No audio, no jog. A track plays only with `--dsp-model`.
- The emulated CDJ is evidence about Pioneer's firmware only as far as the
  emulator is faithful. A wire change in `lib/prolink` still needs the
  captures (`prolink changes: real CDJs first`).

---

## The fork, and upstream

- **`usr-ein/cdj2000-emulator`** is a fork of cdj2k-revival's. Its `main` is
  what TriMixxx pins: upstream main, our changes, and geepot's series merged
  over them. The first pin was made on its `cdj-2k-emu` branch. Once that
  branch merged in TriMixxx, the fork's `main` was fast-forwarded to it.
- **`crew merge` and `crew clean` don't know this repository.** They check
  and move the submodule pointers of prolink, mixxx and ttymidi only.
  - So a change here is pushed before the TriMixxx commit that pins it.
  - Its pin is shown to be reachable: `git -C cdj2000-emulator branch -r
    --contains <pin>`, and a fresh `git clone --recurse-submodules`.
- **Each change for upstream is a commit made directly on upstream main.**
  The fork's `main` merges it in, so each one can go upstream alone:
  `git push origin <sha>:refs/heads/<name>`, then a PR from the fork. The
  first two:
  - `4529093`, *EtherC: a frame shorter than 60 bytes is padded on receive,
    as the wire pads it*
  - `7709d93`, *nxs_vm: --link-hub and --link-mac put the NXS on a Pro DJ
    Link segment*. It needs a small rebase once geepot's series lands: the
    series moves the launcher's Ethernet option into a group of its own.
- **When upstream merges geepot's series,** the fork's `main` moves to
  upstream main plus whatever of ours is still unmerged. The series then
  stops being a merge of its own, and the submodule is bumped like any
  other.
- **Upstream's rule is ours too:** no firmware, no disassembly and no
  screenshots of it in a commit, a PR or an issue. Describe what the
  firmware does in words and measurements.
