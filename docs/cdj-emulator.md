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

This file has two halves. "How it works" describes the feature as the
`cdj-2k-emu` branch will leave it, and stays. "The plan" is what that branch
does to get there, and goes once it is done.

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
- then keep-alives every ~2.7 s; a real deck sends them every 2.0 s

It runs at about 0.74x real time. Presence, numbers, status and media
queries work at that speed: a device times out after 10 s, five missed
keep-alives. Beat-phase sync against a real-time deck does not.

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
    - it refuses while the clone holds uncommitted work, or commits its
      origin lacks
    - otherwise it deinitialises the clone and removes its `modules/` entry
  - git will not remove a worktree that still holds a submodule's gitdir, so
    `git worktree remove` (and `crew clean`) succeed only after this.
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
  Its player screen comes up in about 10 s, and it is on the link in about
  20 s. `up` waits for its first keep-alive and says what it claimed:
  `a: player 1 at 169.254.12.34, 02:43:44:.. on link booth`.
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
pi-qemu link devices booth     # 1 a, 2 b (AUTO numbering), 4 d
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

- About 0.74x real time, and about two cores per CDJ (the TCG thread and the
  DSP thread). Two CDJs and a deck take about five of the Mac's twelve.
- No audio, no jog. Playback runs at DSP speed.
- The emulated CDJ is evidence about Pioneer's firmware only as far as the
  emulator is faithful. A wire change in `lib/prolink` still needs the
  captures (`prolink changes: real CDJs first`).

---

## The plan

### Decisions

- **[Sam] The submodule's base:** a public usr-ein fork holding upstream
  main, geepot's series and ours (14:40: "1a"). He gave his go to create it
  tonight.
- **[Sam] Where it lives:** `cdj2000-emulator/` at the top level ("2a").
- **[Sam] Stage 2 tonight too:** ProLink, "using the upstream changes
  already made" where speed stands in the way ("3c").
- **[Sam] A plan review first** ("4a").
- **[mine] geepot's whole series, not a pick of it.** #38's tip is what
  geepot tested end to end and what was measured here.
- **[mine] The CDJ's Ethernet joins through upstream's own netdev contract.**
  pi-qemu gets no QEMU knowledge of the CDJ, and the emulator none of
  pi-qemu.
- **[mine] The short-frame fix belongs in the emulator's NIC, not in
  pi-qemu.** Any stream, dgram or socket netdev delivers frames unpadded.
  QEMU pads only for slirp and tap, and upstream's own hub had to pad its
  replays for the same reason.
- **[mine] Firmware, and everything firmware-derived, lives outside every
  repository,** in `~/.pi-qemu/cdj/`. Tests that need firmware find it
  through `CDJ_FIRMWARE_DIR`. E1's qtest needs none.
- **[mine] The fork's lasting branch is its `main`,** as in `usr-ein/mixxx`,
  rather than a `trimixxx` branch (plan review round 1). Then `crew merge`
  could later treat this fork like the others.

### Where the commits live

- **The submodule's commits go on the fork's `cdj-2k-emu` branch,** the only
  name the crew's guard lets this minion push. The TriMixxx PR pins its tip.
  - The worktree's own clone of the submodule dies with the worktree. These
    commits don't: they are pushed.
  - Once a pushed TriMixxx commit pins it, `cdj-2k-emu` only moves forward:
    every pinned commit stays on it.
- **`crew merge` checks submodule pointers only for the repos it knows**
  (prolink, mixxx, ttymidi). Nothing would stop TriMixxx main from pinning a
  commit the fork lacks. So the PR shows that the pin is reachable
  ("How it is shown to work", 6).
- **After the merge, Sam's steps,** in the main checkout:
  1. `git submodule update --init cdj2000-emulator`: the main checkout's
     own clone, for worktrees to borrow.
  2. `git -C cdj2000-emulator push origin HEAD:main`: the fork's `main`,
     which `.gitmodules` names, moves to the pin. It is a fast-forward: the
     pin descends from upstream main, where the fork's `main` started.
  3. `pi-qemu/build.sh app`, before `crew clean`: the `pi-qemu` on PATH is
     main's build, and its `worktree release` must know the new submodule.

  Until step 2, the pin stays reachable on `cdj-2k-emu`, which nothing
  deletes. `crew clean` doesn't know the fork, and a recursive clone ignores
  `branch =`.
- **Upstream PR branches.** E1 and E2 are each a commit made directly on
  upstream main, siblings, joined in the fork.
  - So each upstream PR is a branch at that commit, pushed under its own
    name: `git push origin <sha>:refs/heads/<name>`. Sam opens them, and
    the TriMixxx PR carries their texts.
  - The guard refuses other branch names, so they wait as commits until
    then. Both gaps are logged with `crew feedback`.

### Steps

1. **The fork.** `crew gh repo fork cdj2k-revival/cdj2000-emulator
   --clone=false`, then `cdj-2k-emu` built as below and pushed.
2. **E1, on upstream main `a30a097`:** the EtherC model (`cdj_nxs_eth.c`)
   pads a received frame shorter than 60 bytes, instead of flagging it a
   runt.
   - Test: a case in the qtest `tests/test_nxs_ethernet_qemu.py`. A 42-byte
     frame from the peer lands as 60 bytes, with no error bits. Before:
     descriptor `0x78000004`.
   - The qtest needs no firmware any more: the CPU never runs, so a blank
     flash stands in for the BIOS. Upstream's CI can run it wherever QEMU is
     built.
3. **E2, also on upstream main** (a sibling of E1, not stacked): `nxs_vm
   --link-hub PORT|HOST:PORT|unix:PATH` and `--link-mac`, as `boot_vm` has
   them for the CDJ-2000.
   - `boot_vm.link_hub_args` takes the NIC model, and `--link-mac` reuses
     `boot_vm.flash_with_mac`. The flash record at `0x3f8000` is the same on
     NXS 1.44.
   - No `sync:`: the NXS board has no synchronising Ethernet. A bad MAC and
     a second peer are refused before the run directory exists.
   - Tests: command construction, refused combinations.
   - Docs: a RUNNING.md section, "Two NXS decks on one Pro DJ Link". It says
     its measurements need the series #33-#38: on main alone the NXS sent no
     Pro DJ Link packet in 240 s.
   - Once the series lands upstream, E2 needs a small rebase over the
     argument group the series moved. Its PR text tells Sam.
4. **`cdj-2k-emu`:** E1, with E2 merged, then geepot's series
   (`pr2/launcher-defaults`, `30a4196`) merged on top.
   - The merge resolves `nxs_vm.py` and the launch tests.
   - Then the EtherC qtest and upstream's host tests (`pytest -q`) run on
     the result, as they did on E1 and E2 each alone.
5. **T1, the submodule** `cdj2000-emulator/`, with a `.gitmodules` comment
   as `mixxx`'s has: what it is, which branch, why a fork. Also `*.UPD` and
   `*.upd` in TriMixxx's `.gitignore`.
6. **T2, the `cdj` group in pi-qemu:**
   - `firmware`, `build`, `up`, `list`, `status`, `shot`, `press`, `rotary`,
     `rm`, and the hidden `cdj run`
   - a stream-to-datagram port for `Link`
   - the MAC from NAME
   - the Python steps run with `uv run --no-project`, from the submodule
7. **T3: `worktree prepare`, `status` and `release` know
   `cdj2000-emulator`,** including the private clone (see "Worktrees").
   Required: without it, `crew clean` stops at `git worktree remove`.
8. **T4, docs:**
   - `pi-qemu/README.md` gets "Emulated CDJs", the verbs and a booth on a
     link. This file keeps the why: the emulator's base, the firmware
     policy, how the link works.
   - a `CLAUDE.md` row, and the after-merge steps
   - the `trimixxx0` skill: a CDJ on your deck's link
9. **T5, tests:** ctest for the MAC, the stream framing and the verbs'
   arguments; the submodule's pytest.
10. **The checks below,** a re-read of each diff, a rebase onto main, then
    the PR out of draft.

### How it is shown to work

On emulated decks only. No real deck or real CDJ is used.
- Evidence goes in `.crew/evidence/`, described in the PR in words. No CDJ
  screenshot goes into a public PR: its artwork is Pioneer's.
- The CDJs are named after the branch, `cdj-2k-emu-a` and `cdj-2k-emu-b`.
  `crew clean` doesn't know CDJs, so each is `cdj rm`ed once its check is
  done.

1. **The borrow path, before the merge.** In a scratch clone of TriMixxx at
   this branch, with the submodule initialised, standing in for the main
   checkout:
   - a fresh worktree; `worktree prepare` borrows `cdj2000-emulator`
   - `cdj build`, `cdj firmware`, then `cdj up`: the player screen, headless
   - `worktree release`, then `git worktree remove` succeeds
   - in this worktree, whose copy is a private clone: `release` refuses an
     unpushed emulator commit, and once nothing is unpushed it releases
2. **No firmware in git.**
   - `git status` and `git check-ignore` in TriMixxx and the submodule show
     no firmware, and none is tracked. A `C2KNXS.UPD` dropped anywhere in
     either is ignored.
   - Everything extracted is under `~/.pi-qemu/cdj/`.
3. **A CDJ and a deck.** `deck up cdj-2k-emu --link booth`, then
   `cdj up cdj-2k-emu-a --link booth`.
   - `link devices booth` lists both.
   - A `link capture` shows each sending the other its status (UDP 50002),
     and the CDJ answering the deck's media queries.
4. **Two CDJs.** With `cdj up cdj-2k-emu-b --link booth`, `link devices`
   lists 1, 2 and 4, and each CDJ sends the other its status.
5. **E1 and E2 sit directly on upstream main.** Each one's own tests pass
   on its own commit, and each has a PR text.
6. **The pin is reachable.**
   - `git -C cdj2000-emulator branch -r --contains <pin>` names
     `origin/cdj-2k-emu`.
   - A fresh `git clone --recurse-submodules -b cdj-2k-emu` of TriMixxx
     checks the pin out.
7. **Upstream's host tests,** with the firmware outside the checkout
   (`CDJ_FIRMWARE_DIR`). The scratch prototype gave 949 passed, 92 skipped:
   - three skip because they read `ROOT/firmware/` directly
   - the rest are opt-in, or want tools not built here

### Risks

- **geepot's series can change before it merges upstream.** The fork holds
  the commits we pinned, so the pin stays valid. Moving to what upstream
  finally merges is a later bump.
- **Load.** `crew resources` counts decks, not CDJs, and `crew clean`
  removes decks only. A CDJ costs about two cores, more than a deck. The
  minion `cdj rm`s its own CDJs; `crew feedback` asks for the rest.
- **QEMU's configure** must get a Python that can make venvs. `cdj build`
  looks for one and says so if it finds none.
- **macOS's 104-byte socket paths:** the CDJs' sockets live under
  `~/.pi-qemu/cdj/`, short.

### Out of scope

- audio, the jog, sync and beat-phase fidelity, and the emulator's speed for
  its own sake
- a real CDJ or a real deck on the network (bridging the Mac's NIC; geepot's
  `link_hub --bridge` exists)
- bugs the CDJ shows in `lib/prolink`. Each one is noted, with its evidence,
  for a pitch of its own.
