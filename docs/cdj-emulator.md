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
branch `trimixxx`. That branch is three things, in this order:

1. **Upstream main.**
2. **geepot's open PR series, #33 to #38.** Its tip, #38's
   `pr2/launcher-defaults`, merges the whole series. Among them:
   - #35: SH-4 TCG fast paths
   - #36: the DSP on its own thread, and the exact DSP idle skip
   - #37: the fast Blackfin core, `cdj-gui-run`
3. **Our own changes**, each written against upstream main so it can go
   upstream on its own (see "Upstream").

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
  repository,** in `~/.pi-qemu/cdj/`.

### Where the commits live

- **The submodule's commits** are on the fork's `cdj-2k-emu` branch, the
  only name the crew's guard lets this minion push. The TriMixxx PR pins
  its tip.
  - The worktree's own clone of the submodule dies with the worktree. These
    commits don't: they are pushed.
- **After the merge:** `crew merge` and `crew clean` know prolink, mixxx,
  ttymidi and TriMixxx, not this fork.
  - So the pin stays reachable on `cdj-2k-emu`, which nothing deletes.
  - Sam then gives it its lasting name, from the main checkout:
    `git -C cdj2000-emulator push origin <pin>:refs/heads/trimixxx`. That is
    the branch `.gitmodules` names.
  - Logged with `crew feedback`, so the crew can learn this repo.
- **The main checkout needs the submodule initialised once:**
  `git submodule update --init cdj2000-emulator`. `pi-qemu cdj build` does
  it when it is missing. Worktrees then borrow it, as they borrow mixxx (T3).
- **Upstream PR branches.** Each upstream change is a commit made directly
  on upstream main, a first-parent ancestor of `cdj-2k-emu`.
  - So each PR is a branch at that commit, pushed under its own name:
    `git push origin <sha>:refs/heads/<name>`.
  - Sam opens them; the TriMixxx PR carries their texts.
  - The guard refuses other branch names (`crew feedback` again), so they
    wait as commits until then.

### Steps

1. **The fork.** `crew gh repo fork cdj2k-revival/cdj2000-emulator
   --clone=false`. Then `cdj-2k-emu` from upstream main `a30a097`.
2. **E1, `nxs-ethernet-short-frames`:** the EtherC model (`cdj_nxs_eth.c`)
   pads a received frame shorter than 60 bytes, instead of flagging it a
   runt.
   - Test: a case in the opt-in qtest `tests/test_nxs_ethernet_qemu.py`. A
     42-byte frame from the peer lands as 60 bytes with no error bits.
3. **E2, `nxs-link-hub`:** `nxs_vm --link-hub PORT|HOST:PORT|unix:PATH` and
   `--link-mac`, as `boot_vm` has them for the CDJ-2000.
   - `boot_vm.link_hub_args` takes the NIC model, and `--link-mac` reuses
     `boot_vm.flash_with_mac`. The flash record at `0x3f8000` is the same on
     NXS 1.44.
   - No `sync:`: the NXS board has no synchronising Ethernet.
   - Tests: command construction, refused combinations.
   - Docs: a RUNNING.md section, "Two NXS decks on one Pro DJ Link", with
     these measurements.
4. **Merge geepot's series** (`pr2/launcher-defaults`, `30a4196`) into
   `cdj-2k-emu`, resolving `nxs_vm.py` (the series moved its Ethernet
   option). Then run upstream's host tests (`pytest -q`) and the EtherC
   qtest.
5. **T1, the submodule** `cdj2000-emulator/`, with a `.gitmodules` comment as
   `mixxx`'s has: what it is, which branch, why a fork.
6. **T2, the `cdj` group in pi-qemu:**
   - `firmware`, `build`, `up`, `list`, `status`, `shot`, `press`, `rotary`,
     `rm`, and the hidden `cdj run`
   - a stream-to-datagram port for `Link`
   - the MAC from NAME
   - the Python steps run with `uv run --no-project`, from the submodule
7. **T3: `worktree prepare`, `status` and `release` know
   `cdj2000-emulator`.** It is optional: a main checkout without it is
   skipped, with a note.
8. **T4, docs:**
   - `pi-qemu/README.md` gets "Emulated CDJs", the verbs and a booth on a
     link. This file keeps the why: the emulator's base, the firmware
     policy, how the link works.
   - a `CLAUDE.md` row
   - the `trimixxx0` skill: a CDJ on your deck's link
9. **T5, tests:** ctest for the MAC, the stream framing and the verbs'
   arguments; the submodule's pytest.
10. **The done-when checks below,** a re-read of each diff, a rebase onto
    main, then the PR out of draft.

### How it is shown to work

On emulated decks only. No real deck or real CDJ is used. Each check
leaves evidence in `.crew/evidence/`, described in the PR in words: no CDJ
screenshot goes into a public PR, since its artwork is Pioneer's.

1. A fresh worktree runs `pi-qemu worktree prepare`, `cdj build` and
   `cdj firmware`. Then `cdj up a`: the player screen, headless.
2. `git status` and `git check-ignore` in TriMixxx and the submodule show no
   firmware, and none is tracked. Everything extracted is under
   `~/.pi-qemu/cdj/`.
3. `deck up d --link booth`, then `cdj up a --link booth`.
   - `link devices booth` lists both.
   - A `link capture` shows each sending the other its status (UDP 50002),
     and the CDJ answering the deck's media queries.
4. `cdj up b --link booth`: `link devices` lists 1, 2 and 4, and each CDJ
   sends the other its status.
5. E1 and E2 sit directly on upstream main, each with a PR text.

### Risks

- **geepot's series can change before it merges upstream.** The fork holds
  the commits we pinned, so the pin stays valid. Moving to what upstream
  finally merges is a later bump.
- **Load.** `crew resources` counts decks, not CDJs. A CDJ costs about two
  cores, more than a deck (Follow-up).
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
