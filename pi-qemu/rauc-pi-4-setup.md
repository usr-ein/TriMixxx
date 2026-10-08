# TriMixxx updates: RAUC and tryboot on the Pi 4

The plan for how the decks run a locked system and take updates over the air,
and how that is developed and rehearsed on the emulated deck. Written on
2026-10-07. **Nothing here is built yet.**

- **Decisions are settled** (§1).
- **The design is §2 to §9.** Appendices A to C hold the research behind it,
  and sources are at the end. Anything not checked yet is marked
  **unverified**.
- **The work itself** (tasks, files, tests, stop points) is in `PLAN.md`.
  `rauc-pi-4-report.md` explains the system with diagrams.
- Where this differs from `PLAN.md` (D7, D8, §7, §9 and phases P4–P6), this
  file wins.

---

## 1. Decisions

| Decision | Why (details) |
|---|---|
| **RAUC installs updates** | The standard embedded Linux A/B updater, packaged in Debian. It handles checking, writing and bookkeeping, so we don't reinvent them (App. B) |
| **The Pi firmware's own tryboot chooses the slot, not U-Boot** | Firmware, device tree and `config.txt` get A/B protection too. No writes at every boot, the watchdog covers start-up, nothing added to boot time, stock Pi OS boot chain (App. C.2) |
| **System slots are SquashFS** | Can't be modified while running, even by root |
| **The dev card in pi-qemu stays writable** | ext4, today's 2-partition card, for experiments |
| **One image for every deck** | At least four decks: trimixxx1, trimixxx2, trimixxx3 (the spare Pi 4, used as the bench), and more later |
| **Per-deck identity lives on a separate partition** | Name, Wi-Fi and ssh host keys. Written once, read-only until deliberately unlocked |
| **Writes are discarded at every start** | Their RAM use is bounded (§3.3) |
| **Updates are signed** | RAUC requires it. It costs one self-signed key, made once (App. B.6) |

---

## 2. The system at a glance

1. **Two copies of the system on the SD card.** Each copy is a slot: a FAT
   boot partition (firmware, kernel, initramfs, `config.txt`) plus a SquashFS
   system partition.
2. **The Pi firmware picks the slot** through `autoboot.txt` on a small first
   partition. A new slot is started once on trial with the firmware's one-shot
   tryboot flag. Any restart after a failed trial goes back to the old slot.
3. **One boot image serves both slots.** Its `config.txt` picks the right
   kernel command line with Raspberry Pi's `[boot_partition=N]` filter.
4. **RAUC installs updates** (`apt install rauc rauc-service`, 1.13 in trixie).
   It checks the bundle, writes the idle slot and records what it installed.
5. **A health check commits.** Once the deck is up, a systemd unit runs `rauc
   status mark-good` if the MIDI bridge, Mixxx and the sound card all work.
   That makes the new slot the default.
6. **The running system is read-only.** SquashFS cannot be written, and
   Debian's `overlayroot` puts a RAM layer on top that is gone at the next
   start.
7. **Per-deck identity** (name, Wi-Fi, ssh host keys) lives on a small
   read-only `/data` partition that updates never touch.
8. **pi-qemu learns tryboot.** It already plays the Pi firmware's part, so the
   same card and bundle are rehearsed on an emulated deck before trimixxx3,
   then the real decks.

**What we write ourselves** (§6 has the full table):

| Custom piece | Why no standard part covers it |
|---|---|
| RAUC bootloader backend for tryboot, ~60 lines of shell adapted from Rtone's LGPL script | RAUC 1.13 has no Raspberry Pi firmware backend. The upstream one (rauc PR #1599) isn't merged. RAUC's documented custom-backend interface is the intended extension point |
| Health check unit that calls `rauc status mark-good` | RAUC documents this pattern; what counts as healthy is deck-specific |
| Identity unit: hostname and per-deck Mixxx config | No standard tool selects per-device application config |
| Tryboot in pi-qemu: C++ firmware step, one QEMU mailbox patch | QEMU runs no Pi firmware. pi-qemu is the firmware, so it implements tryboot |
| One `make release`: a Dockerfile, `genimage.cfg`, a RAUC manifest | Declarative files that drive standard image tools |

---

## 3. Design

### 3.1 Card layout

MBR with a fixed disk signature, `0x5d0bc1ec` (the first 32 bits of
sha256("trimixxx"), set in `release/Makefile`), so `<id>` below is `5d0bc1ec`:

| # | Label | FS | Size | Holds | Mounted | Written |
|---|---|---|---|---|---|---|
| p1 | bootsel | FAT32 | 64 MiB | `autoboot.txt` only, and never any `start*.elf`: then a missing, empty or garbled `autoboot.txt` starts p2 (measured, PLAN.md T6c and T7) | not mounted (the backend mounts it to commit) | at commit: one small file |
| p2 | boot-A | FAT32 | 512 MiB | firmware, kernel, initramfs, DTBs, overlays, `config.txt`, `cmdline-a.txt`, `cmdline-b.txt` | the running slot read-only at `/boot/firmware` | by RAUC, idle slot only |
| p3 | boot-B | FAT32 | 512 MiB | the same files | | |
| p4 | (extended) | | | container for 5–8 | | |
| p5 | rootfs-A | SquashFS (raw) | 4 GiB | the whole system, zstd-compressed (about 1–1.5 GB today, estimated) | `/`, underneath the RAM layer | by RAUC, idle slot only |
| p6 | rootfs-B | SquashFS (raw) | 4 GiB | | | |
| p7 | data | ext4 | 64 MiB | identity: deck name, Wi-Fi (NetworkManager keyfiles), ssh host keys | `/data`, read-only | when unlocked by hand (`mount -o remount,rw /data`) |
| p8 | state | ext4 | 4 GiB: a bundle (about 1.1 GB) and RAUC's status. The card image ends here, at about 13 GiB, so it flashes in minutes; a bigger card's rest stays unused | RAUC's data directory, downloaded bundles | `/var/lib/rauc`, read-write | by RAUC only |

**Why each partition exists:**
- **p1 on its own:** the file that decides which slot starts is never written
  by an install. Raspberry Pi's own example uses the same arrangement.
- **p8:** RAUC needs a writable directory outside the slots (App. B.7). Keeping
  RAUC's bookkeeping apart from `/data` lets `/data` stay read-only. It's also
  where bundles land.
- **Card size:** real cards can be any size; the image is laid out for 32 GB
  cards. **The power-of-two rule is only for the emulator's card file.**
  pi-qemu rounds the file up as a sparse file (§4.2), which costs no disk
  space.

### 3.2 Boot: one boot image for both slots

`config.txt` is the deck's current file, plus:

```
[boot_partition=2]
cmdline=cmdline-a.txt
[boot_partition=3]
cmdline=cmdline-b.txt
[all]
# The firmware arms the hardware watchdog, and Pi OS's initramfs then keeps
# it armed (App. A.4): a trial that hangs anywhere resets. Value to tune.
kernel_watchdog_timeout=60
```

`cmdline-a.txt` is one line. `cmdline-b.txt` is the same with `-06`,
`rauc.slot=B` and `-03`:

```
console=tty1 root=PARTUUID=<id>-05 rootfstype=squashfs rootwait rootdelay=20 panic=10 overlayroot=tmpfs:recurse=0 rauc.slot=A systemd.mount-extra=PARTUUID=<id>-02:/boot/firmware:vfat:ro
```

Each part of that line comes from a standard component:
- `root=PARTUUID=…`: the kernel.
- `rootdelay=20 panic=10`: initramfs-tools, which mounts the root on Pi OS
  (the kernel's own `rootwait=N` doesn't apply there). It waits 20 s for the
  root, and on a failure reboots after 10 s instead of opening a shell. A
  missing root
  reboots instead of hanging, and a reboot ends the trial.
- `overlayroot=…`: Debian's overlayroot (§3.3). With `recurse=0` it rewrites
  only `/` and leaves `/data` and `/var/lib/rauc` as plain mounts (its
  script, and the release card in pi-qemu: phase 3).
- `rauc.slot=`: RAUC (App. B.4).
- `systemd.mount-extra=`: systemd ≥ 254 (trixie has 257) mounts the slot's own
  boot partition, by PARTUUID like `root=`, with no fstab logic. Checked on
  phase 1's card in pi-qemu.

`autoboot.txt` on p1 is Raspberry Pi's documented example (App. A.2). The
backend always writes it whole, never edits it:

```
[all]
tryboot_a_b=1
boot_partition=2
[tryboot]
boot_partition=3
```

Committing slot B means writing it again with 2 and 3 swapped.

### 3.3 Read-only system, and a bounded RAM layer

**The read-only system**
- The system slot is SquashFS: read-only by construction, even for root.
- Debian's `overlayroot` puts a tmpfs on top. It's the same package and flag
  `raspi-config` uses for its overlay mode on trixie. Everything written lands
  in RAM and is gone at the next start.
- `/etc/initramfs-tools/modules` lists `squashfs` and `overlay`.

**The RAM layer is bounded** with no extra work:
- **The kernel caps it.** overlayroot mounts its tmpfs without a size, and the
  kernel caps such a tmpfs at **half the RAM** (overlayroot's docs: "can grow
  to 1/2 available memory"). That's 2 GB on a 4 GB deck. Beyond that, writes
  fail with "no space"; nothing crashes.
- **zram swap** (2 GiB, already set up by `pi_config/trimixxx-swap-sizes.conf`)
  can compress cold pages from that tmpfs. The release keeps zram only: a swap
  file can't live on a read-only root.

**The writers that would fill it:**

| Writer | Cap |
|---|---|
| Mixxx logs | `/tmp/mixxx`, 2 × 32 MiB |
| journald | Volatile already, capped at 10 % of `/run` by default |
| Core dumps | None: Pi OS lite has no `systemd-coredump`, and the core size limit is 0 |
| Mixxx library and analysis (`~/.mixxx`) | **Measured** (phase 3): about 130 KiB per analysed track, so about 10 MB over a 4-hour set of 80 tracks |
| Mixxx's track cache, tier 1 (`/tmp/trimixxx/cache`) | Copies of the loaded tracks, in `/tmp`'s own tmpfs, not this layer. Bounded by the fork: two thirds of a budget it measures from `/tmp`'s free space (371 MB on the 2 GB emulated board). Evicting drops a copy whose stick is still plugged in |
| Its tier 2 (`~/.cache/Mixxx/trimixxx/tracks`) | In this layer on a release card. Only a copy whose stick was pulled spills there, so at most what tier 1 held; wiped at every start |

**Measured on the emulated release deck** (2 GB board, 10 tracks of five
minutes): the layer grew from 3.7 MB to 5.0 MB, tier 1 to 70 MB, and nothing
spilled. Half the RAM is a generous bound: no tighter cap is needed.

**Only if the measurement calls for it:**
- Set a tighter cap with one `remount,size=` mount option.
- Or move the layer to disk with overlayroot's documented `crypt:` mode. It
  formats a partition afresh at every start, so it uses no RAM, but costs a
  few seconds per start.

### 3.4 One image for every deck

There is nothing per-deck in the system slots. A deck learns who it is as it
starts:

| What | Where | Mechanism |
|---|---|---|
| Wi-Fi | `/data/NetworkManager/` | Standard NetworkManager keyfiles. The identity unit copies them into `/run/NetworkManager/system-connections/`, NetworkManager's own volatile directory, so NetworkManager isn't reconfigured and the dev card (no `/data`) keeps its network. Keyfiles, not netplan: Pi OS's NetworkManager rewrites netplan's profiles into ones that match every NIC (phase 1, PLAN.md §2.1). Changing Wi-Fi means unlocking `/data` |
| ssh host keys | `/data/ssh/` | Standard OpenSSH host keys, copied by the identity unit into `/etc/ssh/` (in RAM) before sshd starts. Generated on the Mac when the deck's card is made (§5.1), so `known_hosts` survives reflashing |
| Deck name → hostname, wiring, accent | `/data/trimixxx.conf`, one line: the deck's name | The identity unit (§6) sets the hostname and links that deck's pre-rendered Mixxx files into place. `units/apply.py` runs at build time for every deck, instead of at deploy time for one |
| The deck's screen (DSI panel or HDMI) | `config.txt` | `[0x<serial>]` sections built from each deck's `units/<deck>.json`, which gains a `serial`. Alternative: the board's own EEPROM `[config.txt]` section (App. A.3) |
| machine-id | none | systemd's standard behaviour on a read-only root: a new one at each start. NetworkManager uses `dhcp-client-id=mac`, so the router keeps giving the same address |

### 3.5 RAUC configuration, the backend and the health check

`/etc/rauc/system.conf`, the same on every deck:

```ini
[system]
compatible=trimixxx-pi4
bootloader=custom
data-directory=/var/lib/rauc

[keyring]
path=/etc/rauc/keyring.pem

[handlers]
bootloader-custom-backend=/usr/lib/rauc/rpi-tryboot

[slot.rootfs.0]
device=/dev/mmcblk0p5
type=raw
bootname=A

[slot.boot.0]
device=/dev/mmcblk0p2
type=vfat
parent=rootfs.0

[slot.rootfs.1]
device=/dev/mmcblk0p6
type=raw
bootname=B

[slot.boot.1]
device=/dev/mmcblk0p3
type=vfat
parent=rootfs.1
```

The bundle manifest:

```ini
[update]
compatible=trimixxx-pi4
version=1.0.1
build=<git describe>

[bundle]
format=plain

[image.rootfs]
filename=rootfs.squashfs

[image.boot]
filename=boot.vfat
```

**Why `plain` bundles first:**
- They work with RAUC 1.13 on any kernel (App. B.4).
- They need neither NBD nor a Range-capable server. The bundle is copied to
  `/var/lib/rauc` and installed from there.

Moving to `verity` (streaming, and later adaptive updates that download only
what changed) is one manifest line, once RAUC ≥ 1.15.1 is in the image.

**The backend, `/usr/lib/rauc/rpi-tryboot`**, starts from Rtone's script, with
three changes:
- `autoboot.txt` lives on p1, not on the boot slot. The script mounts p1 to
  write it.
- It writes the whole file, as the rauc #1599 reviewers asked, then `fsync`,
  rename, and a sync of the directory.
- On a normal (non-trial) start, the health unit checks `autoboot.txt` and
  rewrites it from the running, committed slot if it is invalid, as Raspberry
  Pi's engineer recommends.

| RAUC calls | When | The backend |
|---|---|---|
| `get-primary` | `rauc status` | prints the bootname of `[all] boot_partition` (2 → A, 3 → B) |
| `set-state X bad` | start of `rauc install` | nothing to undo: `[all]` still names the old slot |
| `set-primary X` | end of `rauc install`, or `mark-active` | writes `autoboot.txt` with `[tryboot] boot_partition=` X's boot partition, then arms the flag (`vcmailbox 0x00038064 4 0 1`), so **the next reboot is X's trial** |
| `set-state X good` | `rauc status mark-good` | if X is running on trial, commits: `[all]` becomes X, `[tryboot]` the other slot. Otherwise nothing |
| `get-state X` | `rauc status` | `good` for the running slot, `bad` for the other, as in Rtone's script |
| `get-current` | 1.13 never calls it, since `rauc.slot=` is on the cmdline | bootname from `/chosen/bootloader/partition` |

**The health check, `trimixxx-health.service`**
- Runs only on a trial start. `ExecCondition=` checks the device tree's
  `tryboot` is 1, as Home Assistant OS does.
- Starts after NetworkManager and ssh, per the never-lock-the-deck-out rule.
- Within `TimeoutStartSec=180`, it checks:
  - the MIDI bridge runs;
  - Mixxx opened the sound card and the S3 controller (the checks
    `instance.sh ready` makes today);
  - `/etc/trimixxx-release` matches the bundle version.
- On success it runs `rauc status mark-good`.
- On failure or timeout it runs `rauc status mark-bad`, and
  `FailureAction=reboot` brings back the committed slot.

**Covered without code:**
- Kernel panics: `panic=10`.
- Hangs: the firmware and kernel watchdog, plus the `RuntimeWatchdogSec=1min`
  that Raspberry Pi OS already sets.
- Power cuts: the trial flag dies with the power.

---

## 4. Local development in QEMU

### 4.1 Two emulated decks, one build

| Emulated deck | Card | Use |
|---|---|---|
| **Dev deck** (today's trimixxx0) | `image/build.sh` card: 2 partitions, ext4, writable | Feature work: `instance.sh deploy NAME mixxx\|config\|system…` in seconds. Unchanged |
| **A/B deck** (new) | `trimixxx-<v>.img` from `make release`: the 8-partition card, locked | Update work: RAUC, the backend, the health check, rollback, fault injection |

**They run the same system.** Everything the release needs is installed by
the normal build, so the dev deck carries it too:
- overlayroot;
- RAUC and its configuration;
- the backend;
- the health and identity units;
- the fstab entries for `/data` and `/var/lib/rauc` (`nofail`).

All of it stays idle on the dev card, for three reasons:
- the command line has no `overlayroot=`;
- `/data` and `/var/lib/rauc` don't exist there;
- `rauc.service` is D-Bus activated and nothing calls it.

The release only seals the system: it leaves out trimixxx0's identity. **What
you test is what ships.**

### 4.2 What pi-qemu needs

pi-qemu is the Pi firmware for the emulated deck. Each change below copies a
documented firmware behaviour, so nothing invented reaches the card.

1. **Understand `[boot_partition=N]` and `[partition=N]` in `config.txt`.**
   `filterMatches()` knows only `[all]`, `[pi4]`, `[board-type]` and
   `[tryboot]` today.
2. **Honour tryboot:**
   - a one-shot flag, cleared after use;
   - it selects `autoboot.txt`'s `[tryboot]` section, and `[tryboot]` in
     `config.txt`;
   - without `tryboot_a_b=1`, read `tryboot.txt` instead of `config.txt`;
   - honour `reboot N`, the partition the kernel writes to the watchdog's
     `PM_RSTS` register.

   Today `Machine::powerOn()` always passes `tryboot = false`.
3. **Add a QEMU patch** in `trimixxx-patches.py`:
   - Implement the firmware messages SET_REBOOT_FLAGS (`0x00038064`) and
     GET_REBOOT_FLAGS (`0x00030064`). Today they fall through to
     "unimplemented".
   - The flags survive the guest's reset, and are readable over QMP with
     `PM_RSTS`.
   - pi-qemu then runs QEMU with `-action reboot=shutdown,shutdown=pause`
     instead of `-no-reboot`, reads both values at each reboot, and starts the
     board again.

   This is `PLAN.md`'s Q2. It makes `vcmailbox` and a reboot to `"0 tryboot"`
   behave as on a Pi. On trixie that reboot is
   `systemctl reboot --reboot-argument="0 tryboot"`.
4. **Write `/chosen/bootloader/{tryboot,partition,boot-mode,pm_rsts,capabilities}`**
   into the device tree, as the firmware does. The backend and the health
   check read them. Today pi-qemu writes none, and
   `/proc/device-tree/chosen/bootloader` doesn't exist on trimixxx0.
5. **Fall back like `PARTITION_WALK`.** If `autoboot.txt` is missing or
   invalid, boot the first FAT partition that holds `start4.elf`. Today it
   boots the first FAT partition.
6. **Accept cards of any size.** Round the card up to the next power of two as
   a sparse copy (an APFS clone, then `truncate`), so a release card boots as
   it is.

`instance.sh up NAME --from trimixxx-<v>.img` already boots any card. A golden
snapshot of the A/B card can come later, if quick restores matter there.

### 4.3 The update-work loop

```
make release VERSION=1.0.0                    # card + bundle (§5.2)
pi-qemu/instance.sh up ab --from out/1.0.0/trimixxx-1.0.0.img
make release VERSION=1.0.1                    # the change under test
scp out/1.0.1/trimixxx-1.0.1.raucb ab:/var/lib/rauc/
ssh ab sudo rauc install /var/lib/rauc/trimixxx-1.0.1.raucb
ssh ab sudo reboot                            # trial start of B
ssh ab rauc status                            # B committed once the health check passed
```

`ssh ab` and `scp … ab:` stand for the instance's own alias. Wrap them in
`pi-qemu/instance.sh run ab -- …`, which puts the instance's ssh and scp
first in `PATH`.

The fault matrix to run before the bench or any deck sees a change:

| Fault | How, in the emulator | Expected |
|---|---|---|
| Plug pulled during the install | `instance.sh ctl ab power off` mid-`rauc install` | A starts; `rauc status` shows B unfinished |
| Plug pulled after install, before reboot | power off, power on | A starts: the flag dies with the power |
| Health check fails | a bundle with a broken Mixxx config | trial → mark-bad → restart → A |
| Kernel panic on the trial | a bundle with a broken initramfs | `panic=10` → restart → A |
| Hang on the trial | a bundle whose session never starts | watchdog → restart → A |
| Plug pulled during the commit | power off at mark-good | A damaged `autoboot.txt` starts A (measured, PLAN.md T6c and T7). The backend's `repair` restores its copy, and reboots into B if B was committed (PLAN.md §5.2) |
| Rollback | `rauc status mark-active other`, then reboot | trial of the old version → commit |

### 4.4 What QEMU cannot prove

Anything that depends on the real `start4.elf`, EEPROM or SD card:
- that a Pi 4's firmware honours `[boot_partition=N]`;
- what it does with a corrupt `autoboot.txt`;
- EEPROM versions and settings;
- real watchdog timing;
- an SD card losing power mid-write.

**That's what trimixxx3 is for** (§7). pi-qemu's tryboot is checked against
what trimixxx3 shows, not the other way round.

---

## 5. Shipping to units

### 5.1 Once per deck: commissioning

1. **Bootloader.** On the deck's current OS, run `sudo rpi-eeprom-update -a`
   and reboot.
   - The bootloader must be from 2022-12 or later; aim for the 2026-09
     default.
   - Check its settings with `rpi-eeprom-config`: `PARTITION_WALK` (on by
     default), `BOOT_WATCHDOG_TIMEOUT`, and on the bench `BOOT_UART=1`.
2. **Record the board's serial** in `units/<deck>.json`, for its screen
   section (§3.4).
3. **Back up** anything that lives only on the deck (PLAN.md P0).
4. **Make the deck's card:** `make card DECK=trimixxx3`. That's the release's
   card image with this deck's `/data` filled in:
   - its name;
   - its Wi-Fi as a NetworkManager keyfile;
   - ssh host keys generated on the Mac.

   The secrets stay on the Mac in a gitignored directory. Only this file
   carries them; bundles never do.
5. **Flash it once**, with Raspberry Pi Imager ("Use custom") or
   `bmaptool copy`, and start the deck. It runs slot A, committed.

### 5.2 Every release

1. **Tag the commit**, then run `make release VERSION=1.0.1`. It refuses a
   dirty tree, and runs:
   - `image/build.sh` (existing) at that commit, giving the writable card with
     everything installed;
   - **one Docker container**, pinned by a `Dockerfile`: debian:trixie with
     genimage, rauc, squashfs-tools, dosfstools, mtools and e2fsprogs. It runs
     privileged to loop-mount that card read-only, then runs `genimage` with
     **one `genimage.cfg`**.

   genimage produces:
   - `rootfs.squashfs`: the system tree minus a checked-in exclude list (the
     emulated deck's hostname, host keys and `pi-qemu-home` network profile);
   - `boot.vfat`: the boot files plus `config.txt`, `cmdline-a.txt` and
     `cmdline-b.txt`, all checked in;
   - `bootsel.vfat`: `autoboot.txt`;
   - `trimixxx-<v>.img`: the 8-partition card with its fixed disk signature.
     B and `/data` are left empty; `make card` fills `/data` per deck;
   - `trimixxx-<v>.raucb`: built with genimage's `rauc` image type, or
     `rauc bundle`, from the manifest in §3.5 and the self-signed key.

   Everything lands in `out/<v>/` with a manifest (commit, checksums,
   `dpkg -l`). Every release is kept.
2. **Rehearse in QEMU:** §4.3, against the previous release's card.
3. **Bench:** the same commands against trimixxx3, watching the bootloader on
   its serial line.
4. **Ship:** the same commands against each deck. `make ship DECK=trimixxx2
   VERSION=1.0.1` runs exactly these:
   ```
   scp out/1.0.1/trimixxx-1.0.1.raucb trimixxx2:/var/lib/rauc/
   ssh trimixxx2 sudo rauc install /var/lib/rauc/trimixxx-1.0.1.raucb
   ssh trimixxx2 sudo reboot
   ssh trimixxx2 rauc status          # once it's back: the new slot committed, or the old one running
   ```
5. **Roll back** with `ssh <deck> sudo rauc status mark-active other` and a
   reboot. The previous version gets a trial like any update. Older versions
   are just older bundles.

**What makes it repeatable:**
- Releases come only from a clean, tagged commit.
- The inputs are pinned: the Pi OS image by checksum, QEMU, the Docker images.
- apt moves, so the manifest records every installed package.
- Every version's files are kept.

**What makes it safe:**
- The running slot is never written.
- A new slot becomes the default only after it has started and passed the
  health check.
- Every failure path (§4.3) ends on the old slot, without anyone touching the
  deck.

---

## 6. Standard parts, and why each custom one exists

| Job | Standard part | Custom? | Why, and how it goes away |
|---|---|---|---|
| Choose the slot, trial start, fall back | Pi firmware: `autoboot.txt`, tryboot (Raspberry Pi docs) | no | — |
| Command line per slot | `config.txt` `[boot_partition=N]` (Raspberry Pi docs) | no | Removes the install hook other setups need |
| Check, install and record updates | RAUC 1.13 (Debian) | no | — |
| Connect RAUC to the Pi firmware | RAUC's custom backend interface | **yes, ~60 lines, adapted from Rtone (LGPL)** | RAUC 1.13 has no Pi firmware backend. Deleted once RAUC with `bootloader=raspberrypi` (PR #1599, aimed at 1.17) is in the image |
| Confirm a boot | `rauc status mark-good` from a systemd unit (RAUC's documented pattern) | **the checks only** | What "healthy" means is deck-specific: the checks `instance.sh ready` makes today |
| Read-only system, RAM layer | SquashFS + Debian `overlayroot` (what `raspi-config` uses) + `/etc/initramfs-tools/modules` | no | — |
| Resets on hangs and panics | Firmware `kernel_watchdog_timeout`, systemd `RuntimeWatchdogSec`, kernel `panic=` | no (settings) | — |
| Mount the running boot slot | systemd `systemd.mount-extra=` | no (setting) | — |
| Wi-Fi, ssh keys, the `/data` lock | Standard NetworkManager keyfiles and OpenSSH host keys (copied into RAM by the identity unit), an fstab `ro` mount, `mount -o remount` | no (formats and settings) | — |
| Who this deck is | — | **yes, one small unit** | Choosing per-device app config (hostname, wiring, accent) has no standard tool. It's the one TriMixxx-specific step at start |
| Build card images and bundles | genimage + mksquashfs + `rauc bundle` (Debian) | **one `genimage.cfg`, one `Dockerfile`, one manifest, `make release`** | Declarative files. The only script is the Make target that runs them |
| Build the system itself | `image/build.sh` + `deploy.sh` | existing | Unchanged: it already builds the dev card |
| A/B in the emulator | pi-qemu (our firmware emulation) + QEMU | **yes, C++ and one QEMU patch** | QEMU has no Pi firmware, and pi-qemu exists to be it. Everything it does copies documented firmware behaviour |
| Ship | `scp`, `rauc install`, `reboot` | no | `make ship` only runs those three commands |

**New files, in all**
- On the deck:
  - `/etc/rauc/system.conf`;
  - the backend;
  - the health unit and its check;
  - the identity unit;
  - a few drop-ins and settings (`coredump.conf`, initramfs modules, swap,
    the fstab lines for `/data` and `/var/lib/rauc`);
  - two command lines and an `autoboot.txt`.
- On the Mac:
  - a `Dockerfile`;
  - `genimage.cfg`;
  - a manifest template;
  - an exclude list;
  - three Make targets (`release`, `card`, `ship`).
- In pi-qemu: the changes in §4.2.

No per-component upload scripts are added. The existing ones stay the dev
loop's tools.

---

## 7. The bench deck: trimixxx3

trimixxx3 is a bare Pi 4 with an SD card, standing in for a future deck.

**Hardware**
- **A USB-serial adapter** on GPIO 14/15 and GND, with the EEPROM's
  `BOOT_UART=1`. It shows every decision the bootloader makes, which is what
  to watch when A/B misbehaves. There is no S3 sharing the UART.
- **A USB sound card** like the decks' UCA222. The health check needs Mixxx's
  sound stream; without one, every trial would fail.
- **An HDMI screen.** Its `config.txt` section picks HDMI instead of the DSI
  panel, which also tests the per-deck screen mechanism.
- **Wi-Fi** comes from its `/data` (§5.1). On the decks, eth0 is the CDJ port,
  link-local only.

**First, check the firmware facts, before any RAUC work.** A small card with
three FAT partitions, Pi OS boot files and `autoboot.txt` is enough. It takes
an afternoon and settles every unverified point the design rests on:

| Check | Expected |
|---|---|
| `[boot_partition=N]` picks the command line on a Pi 4 | p2 → `cmdline-a.txt`, p3 → `cmdline-b.txt` |
| `reboot '0 tryboot'` and `vcmailbox 0x00038064 4 0 1` | next start on p3; `/proc/device-tree/chosen/bootloader/tryboot` = 1; any later reboot → p2 |
| Power-off after arming | p2 |
| `autoboot.txt` deleted, then truncated | p2. **Measured:** p2, because partition 0 is the first FAT partition with firmware, not because of the partition walk. So p1 must hold none (T6b, T6c, T7) |
| `kernel_watchdog_timeout` and a hang in the initramfs | reset, then p2 |
| `kernel_watchdog_partition` or EEPROM `BOOT_WATCHDOG_PARTITION`, then a hang on the **committed** slot | Does the reset start the other slot? If it does, tryboot covers U-Boot's one real advantage (App. C.2). **Measured:** not for a hang in Linux (T9); the EEPROM's does for a failure before Linux, to a fixed partition (T10) |

The results go back into this file and into pi-qemu's emulation (§4.2).

---

## 8. Phases

The tasks, commands, tests and stop points for each phase are in `PLAN.md`.

| # | Phase | Delivers | Done when |
|---|---|---|---|
| 1 | Firmware checks on trimixxx3 (§7) | The results table above, filled in | Every **unverified** firmware point in App. A has a measured answer |
| 2 | Tryboot in pi-qemu (§4.2) | QEMU reboot-flags patch; firmware step: filters, tryboot, `/chosen/bootloader`, partition walk, any-size cards | Phase 1's card boots in pi-qemu, and the same checks give the same results as on trimixxx3 |
| 3 | A locked card (§3, §5) | overlayroot, initramfs modules, `/data` and state mounts, the identity unit, the boot splash's slot label (`A`/`B`, `trial`, the version); `Dockerfile`, `genimage.cfg`, exclude list, `make release` and `make card` | A tag produces a card that boots locked in QEMU, and the RAM layer's growth over a set's worth of tracks is measured (§3.3) |
| 4 | RAUC on the deck (§3.5) | Signing key, `system.conf`, the backend, the health check, the bundle in `make release`, `make ship` | The whole fault matrix (§4.3) passes in QEMU |
| 5 | trimixxx3 end to end | Its card, two shipped releases | The fault matrix passes on hardware |
| 6 | The decks | Each deck commissioned and reflashed once (§5.1) | Every deck has taken one release over the air |

---

## 9. Open questions

- **Where per-deck screen settings live:**
  - `[0x<serial>]` sections in `config.txt`: in git, recommended;
  - or each board's EEPROM `[config.txt]` section: kept with the hardware,
    outside git.
- **When to move to verity bundles and streaming.** That needs RAUC ≥ 1.15.1
  in the image, from Debian forky, a backport, or waiting for 1.17, which may
  also bring the native Pi backend.
- **Can a privileged Docker container on macOS loop-mount the card?** If not,
  the build VM streams its sealed root out as a tar over ssh, and `mksquashfs
  -tar` reads it.
- **Must `autoboot.txt` still fit in 512 bytes?** That limit was dropped from
  the docs in 2026-09. Ours is about 60 bytes either way.
- **Should the dev card boot from SquashFS later?** Not for now: Sam wants it
  writable.
- **Should the decks set the EEPROM's boot watchdog?** **Decided: yes** (Sam,
  2026-10-08), with `BOOT_WATCHDOG_TIMEOUT=45` and `BOOT_WATCHDOG_PARTITION=2`,
  and a reconcile step in the backend for the fallback (PLAN.md §5.2). On trimixxx3,
  `BOOT_WATCHDOG_TIMEOUT=45` with `BOOT_WATCHDOG_PARTITION=3` turned a
  corrupt `start4.elf` on p2 into a start of p3 (PLAN.md T10). But the
  partition is fixed, so it only helps while A is the committed slot.
  **Unverified:** whether a conditional EEPROM section can point it at the
  other slot. The kernel-stage equivalent, `kernel_watchdog_partition`,
  didn't switch slots (T9).

---

## Appendix A. How a Pi 4 does A/B natively (research)

### A.1 The start-up chain
The Boot ROM starts the bootloader in the SPI EEPROM on the board, which then:
1. reads `autoboot.txt` from the first FAT partition;
2. loads `start4.elf` from the chosen boot partition.

`start4.elf` reads `config.txt`, then loads the kernel, device tree, overlays,
initramfs and command line.

There is no BIOS and no GRUB. `config.txt` does the job of `grub.cfg`, and
`cmdline.txt` holds the kernel line.

### A.2 The pieces Raspberry Pi documents for A/B

**`autoboot.txt`**, on the first FAT partition
- Holds `boot_partition=N`, `tryboot_a_b=1` and a `[tryboot]` section. Only the
  `[all]`, `[none]` and `[tryboot]` filters are allowed.
- With `tryboot_a_b=1`, the tryboot partition starts with its normal
  `config.txt`; without it, `tryboot.txt` is read.
- Raspberry Pi's example is the file in §3.2. If the trial passes, swapping
  the two numbers commits it. If it fails, "a standard reboot clears the
  tryboot flag, reverting to the default partition".

**The tryboot flag**
- Set by `sudo reboot '0 tryboot'`, which on trixie is
  `sudo systemctl reboot --reboot-argument="0 tryboot"` (measured). "All
  Raspberry Pi models support tryboot".
  On a Pi 4 rev 1.0/1.1 "the EEPROM must not be write protected".
- The kernel can also arm it without restarting: `vcmailbox 0x00038064 4 0 1`,
  the SET_REBOOT_FLAGS firmware message, sent through `/dev/vcio`.
- It survives a reboot but **not a power-off**. A power cut always returns to
  the committed slot.

**`[boot_partition=N]` in `config.txt`**
- It "can be used to select alternate OS files (for example, `cmdline.txt`) to
  be loaded, depending on which partition `config.txt` was loaded from".
- Raspberry Pi's example is the A/B case:
  `[boot_partition=2] cmdline=cmdline_rootfs_a.txt`, and the same for B.
- So one boot image works in either slot.

**What the OS can read.** The result is under
`/proc/device-tree/chosen/bootloader/`, as 32-bit big-endian values:
- `tryboot`: 1 during a trial;
- `partition`: the boot partition used;
- `boot-mode` and `pm_rsts`;
- `capabilities`: bit 2 is TRYBOOT_A_B, bit 3 is TRYBOOT.

**`sudo reboot N`** starts partition N once. The `[partition=N]` filter
matches it.

### A.3 Bootloader (EEPROM) versions for the Pi 4

| Feature | Since |
|---|---|
| tryboot | 2020-10-28 (beta) |
| `[tryboot]` filter and `tryboot_a_b` | 2022-10-18 beta, default since 2022-12-01 |
| Fix: tryboot flag lost with secure boot | 2024-04-15 |
| `[boot_partition=N]` in `config.txt` | start4.elf from about 2025-03 (rpi-update); trixie's firmware is newer. **Verified on a Pi 4** (trimixxx3, 2026-10-08): p2 → `cmdline-a.txt`, p3 → `cmdline-b.txt` |
| `PARTITION_WALK` (missing or invalid boot partition → try the next) | 2025-02-11, on by default since 2025-08-13 |
| `BOOT_WATCHDOG_TIMEOUT` / `BOOT_WATCHDOG_PARTITION` | 2025-07-03 |
| Current default release | 2026-09-23 |

One meta-rauc contributor saw the tryboot flag fail with a 2021-04 bootloader
and work with a 2023-01 one. That's why every deck gets the current bootloader
before its first flash (§5.1).

**Measured on trimixxx3** (PLAN.md §2.3, 2026-10-08):
- **A hung trial falls back.** With `kernel_watchdog_timeout=60`, the firmware
  passes `watchdog.open_timeout=60`, Pi OS's `rpi_wd` keeps the watchdog
  armed, and a trial stuck in the initramfs was reset to A (T8). After a
  commit, the same hang just restarts the committed slot (T9).
- **A 2021-04-29 bootloader starts slot B.** It reads `autoboot.txt` but not
  its sections, so the last `boot_partition=` (3, from `[tryboot]`) wins. A
  release card on such a board would start its empty slot B. Updating the
  bootloader first (§5.1) is a hard requirement, not a precaution.
- **Updated to 2026-05-17**, every A/B rule behaved as documented: `[all]`
  gives p2, `[tryboot]` gives p3 for one start, and the device tree's
  `partition` and `tryboot` say which.
- **Arming:** `vcmailbox 0x00038064 4 0 1` works, and `0x00030064` reads it
  back. On trixie, systemd 257 refuses `reboot '0 tryboot'` ("Too many
  arguments"). The form is `systemctl reboot --reboot-argument="0 tryboot"`.

**EEPROM settings for development**
- `BOOT_UART=1` logs the bootloader's decisions on GPIO 14/15.
- `uart_2ndstage=1` in `config.txt` makes `start4.elf` log too.
- An EEPROM `[config.txt]` section is appended in memory to every
  `config.txt`, which gives per-board settings independent of the slots.
  **Unverified** on our boards.

### A.4 Pitfalls others hit

- **The commit is a FAT write**, and FAT has no journal.
  - Raspberry Pi's engineer recommends keeping a journal on the data
    partition, and checking `autoboot.txt` at start.
  - Others write a new file, `fsync` it, then rename.
  - If `autoboot.txt` is missing or invalid, the firmware uses partition 0,
    the first FAT partition holding a `start.elf`. With no firmware on p1,
    that's p2, so the deck still starts (measured, PLAN.md T6c and T7).
    `PARTITION_WALK` isn't involved; it only applies to a requested partition.
    With firmware files on p1 but no `start4.elf`, the deck stays dark (T6b).
  - It starts p2 whichever slot was committed, which is why the backend
    keeps a copy of `autoboot.txt` on the state partition (PLAN.md §5.2).
  - If `autoboot.txt` is **valid** but names an unbootable partition, nothing
    recovers it. So only a slot that has already started is ever committed.
- **GPT on a Pi 4.** Choosing a boot partition other than the first "never
  worked with GPT on Pi4" until a February 2025 fix, and one project still saw
  it fail in June 2026. **Use MBR**, with the FAT partitions first and
  contiguous.
- **`rpi-eeprom-update`** stages files on `/boot/firmware`, which is now the
  active slot. A `recovery.bin` on p2 broke booting (rpi-eeprom #499). Mask the
  service in the release image and update EEPROMs deliberately (§5.1).
  - The Pi 4's ROM runs `recovery.bin` from p1.
  - On an A/B card, stage the update there:
    `mount /dev/mmcblk0p1 /mnt; BOOTFS=/mnt rpi-eeprom-update -a`.
    That's measured on trimixxx3.
  - Then `BOOTFS=/mnt rpi-eeprom-update -r` clears the leftovers.
- **Pi OS's initramfs disarms the hardware watchdog** (`rpi_wd`) unless
  `kernel_watchdog_timeout` is set in `config.txt`. Set it, so that a trial
  that hangs early still resets.
- **A SquashFS root on Pi OS needs the initramfs to load `squashfs`**, which
  the Pi kernel builds as a module. List it in `/etc/initramfs-tools/modules`,
  the documented place.
- **Pi OS's first boot rewrites the disk ID** (`set_partuuid`, triggered by
  `resize` on the command line), which breaks `PARTUUID=` roots. The release
  command line must not carry `resize`, and the card image uses a fixed disk
  signature.
- **`config.txt` filters of different kinds are ANDed.** End each block with
  `[all]`.

---

## Appendix B. RAUC (research)

### B.1 What it is
RAUC is a client on the device plus a host tool. On the device it:
- receives a signed **bundle**;
- checks it;
- writes the images into the **inactive slot group**;
- records what it installed;
- asks a **bootloader backend** to boot the new group next.

`rauc status mark-good` then confirms the boot. RAUC is maintained by
Pengutronix, packaged by Debian, and used by Home Assistant OS, among others.

### B.2 Debian trixie packaging
- **`rauc` 1.13-3+deb13u1** contains `/usr/bin/rauc`, built with GPT and HTTP
  streaming support.
- **`rauc-service`** contains `rauc.service`
  (`ExecStart=/usr/bin/rauc --mount=/run/rauc/mnt service`). It is D-Bus
  activated, not enabled, and ships with its D-Bus activation and policy
  files.
- **No mark-good unit ships**, from Debian or upstream. Upstream only
  documents an example.
- **No `system.conf` ships.** We provide `/etc/rauc/system.conf`. The search
  path is `/etc/rauc`, then `/run/rauc`, then `/usr/lib/rauc`.
- Debian sid/forky have 1.15.2. There is no trixie backport.

### B.3 Kernel support
The Pi OS kernel (`bcm2711_defconfig`, rpi-6.18.y) has:
- `BLK_DEV_LOOP=y`;
- `SQUASHFS=m`, with XZ, ZSTD and LZO but not LZ4;
- `MD=y`, `BLK_DEV_DM=m`, `DM_VERITY=m`, `DM_CRYPT=m`;
- `BLK_DEV_NBD=m`;
- `OVERLAY_FS=m`, `ZRAM=m`, `BCM2835_WDT=y`.

That covers every RAUC bundle format (plain, verity, crypt), and HTTP
streaming, which needs NBD. Whether `nbd` loads on its own is **unverified**;
list it in `/etc/modules-load.d/` if streaming is used.

### B.4 Version traps in 1.13
- **Detecting the booted slot.** 1.13 uses `rauc.slot=` on the kernel command
  line first, then `root=`, and only then the backend's `get-current`. 1.15
  puts the backend first. So each slot's command line carries `rauc.slot=A`
  or `B`, which wins in every version.
- **dm-verity on Linux 6.19+.** RAUC 1.13 expects a status string that Linux
  6.19 changed, so verity installs fail; 1.15.1 fixed it. Pi OS ships 6.18
  today. Plain bundles don't use dm-verity.

### B.5 What the custom backend sees
RAUC 1.13 runs the backend executable with:
- `get-primary`
- `set-primary <bootname>`
- `get-state <bootname>`
- `set-state <bootname> good|bad`
- `get-current`

The `get-*` calls print their answer to stdout; the `set-*` calls are judged
on their exit code.

| RAUC action | Backend calls |
|---|---|
| `rauc install` | `set-state <target> bad` before writing, `set-primary <target>` after |
| `rauc status mark-good` | `set-state <booted> good` |
| `rauc status mark-bad` | `set-state <booted> bad` |
| `rauc status mark-active <slot>` | `set-primary <slot>` |
| `rauc status` | `get-primary`, plus `get-state` for every slot with a bootname |

- **Where bootnames go.** Only slots **without** `parent=` may have a
  bootname. So the rootfs slots carry `bootname=A|B`, and the FAT boot slots
  are their children (`parent=rootfs.N`); RAUC writes each pair together.
- **One-shot trial bootloaders aren't covered by the docs.** The closest model
  is the EFI backend: "primary" means boot next time only (`BootNext`), and
  "good" makes it permanent. That is exactly tryboot plus a commit.

### B.6 Bundles and signing
- **Manifest sections:**
  - `[update]`: `compatible=`, `version=`, `build=`;
  - `[bundle]`: `format=plain|verity|crypt`;
  - `[image.<class>]`: `filename=`.
- **Signing is mandatory.** "For development purpose a self-signed certificate
  might be sufficient". One `openssl req -x509` key pair, made once, is a
  documented setup.
- **`rauc bundle --cert --key <dir> <out.raucb>` runs unprivileged.** It needs
  only `mksquashfs`, its own dm-verity code and OpenSSL, so it works in a plain
  Docker container. Two limits:
  - in 1.13 the input directory may hold only plain files, no
    subdirectories;
  - RAUC hard-links files into a work directory, so stage them inside the
    container rather than on a macOS bind mount.
- **The file extension picks how an image is written** (1.13):
  - `.squashfs` → `type=raw` slot: block copy;
  - `.vfat` or `.img` → `type=vfat` slot: block copy, size-checked;
  - `.tar*` → vfat or ext4 slot: mkfs, then unpack.
- Identical images are skipped (`install-same=true` by default).

### B.7 Status and data directory
- RAUC records installs in `<data-directory>/central.raucs`. It must be
  **writable during an install**, or "the installation will be aborted".
- At service start RAUC also writes the boot ID; if that fails, it only warns.
- The data directory "must be located on a non-redundant filesystem which is
  not overwritten during updates". It also holds the data for adaptive
  updates.
- For read-only slots, RAUC's FAQ says to "use `type=raw` … and use a shared
  data directory on a separate non-redundant partition".

### B.8 Streaming, or installing from a file
- **Streaming** (`rauc install https://…`) needs:
  - verity bundles;
  - NBD in the kernel;
  - a server that supports HTTP Range requests (Python's `http.server`
    doesn't).

  Debian's build streams every http(s) URL; there is no download fallback.
- **From a file:** copy the bundle to the deck and run `rauc install
  /path/x.raucb`. The bundle is loop-mounted, not unpacked, so it needs only
  its own size in space.

---

## Appendix C. Other integrations, and U-Boot vs tryboot (research)

### C.1 How others do it

| Project | Bootloader | Layout | Notes |
|---|---|---|---|
| **meta-rauc-raspberrypi** (Yocto, RAUC's community layer) | U-Boot, "for demo purpose only" | p1 shared FAT, p2/p3 ext4 rootfs, data | Boot counting in `boot.scr`. Draft **PR #181** moves it to tryboot with our layout: p1 `autoboot.txt`, p2/p3 boot, p5/p6 rootfs, `[boot_partition=N] cmdline=…` |
| **Rtone/raspberrypi-firmware-rauc-bootloader-backend** | Pi firmware tryboot | boot slots are children of rootfs slots | Bash, LGPL-2.1, used "on several dozens remote devices". `set-primary` arms the flag with `vcmailbox`; mark-good swaps `autoboot.txt`; it detects a trial from the device tree. **The basis of our backend** |
| **rauc PR #1599** (native `bootloader=raspberrypi`) | Pi firmware tryboot | — | Open, milestone v1.17. Reviewers asked for fixes, among them generating `autoboot.txt` from scratch. **When it ships, our backend is deleted** |
| **geekdojo/rasputin-os** (Buildroot, AGPL) | Pi firmware tryboot | MBR: p1 selector, p2/p3 FAT boot, p4 extended, p5/p6 SquashFS raw slots, p7 persistent | Nearly our layout: fixed disk signature, `rauc.slot=` on the cmdline, a data directory |
| **Home Assistant OS** | U-Boot on Pi 4, tryboot on Pi 5 | — | Arms the trial with `echo "0 tryboot" > /run/systemd/reboot-param`, so any `systemctl reboot` becomes a trial. Detects it with `cmp -s -n 4 …/chosen/bootloader/tryboot /dev/zero` |
| **rpi-image-gen `image-rota`** (Raspberry Pi's own) | Pi firmware tryboot | GPT: autoboot, boot_a/b, system_a/b (erofs), persistent | A slot mapper (udev + initramfs) gives a slot-agnostic `root=`. The updater is Raspberry Pi Connect OTA (experimental), **not RAUC**. Uses GPT, which App. A.4 advises against on a Pi 4 |

### C.2 U-Boot or the Pi's own tryboot? → tryboot

**What U-Boot would look like on the decks**, based on RAUC's demo layer
(meta-rauc-raspberrypi):
- U-Boot replaces no Pi boot stage. The firmware still runs, reads
  `config.txt` and loads U-Boot as if it were the kernel
  (`kernel=u-boot.bin`).
- One shared FAT partition holds the firmware, `config.txt`, the device tree,
  U-Boot, its boot script and its environment (`uboot.env`).
- The boot script counts attempts per slot (`BOOT_A_LEFT=3`). On **every
  boot** it decrements the count and runs `saveenv`, then loads the kernel
  from the chosen rootfs, using the device tree the firmware already loaded
  (`${fdt_addr}`).
- RAUC drives it with its built-in `bootloader=uboot` backend, through
  `fw_setenv`.
- U-Boot itself is packaged in Debian trixie (`u-boot-rpi` 2025.01, with an
  `rpi_4` build).

| | Pi firmware tryboot | U-Boot | What it means for the decks |
|---|---|---|---|
| **What updates A/B** | Everything: firmware, kernel, device tree and overlays, `config.txt`, cmdline, initramfs, rootfs | Rootfs and kernel. Firmware, device tree, overlays, `config.txt` and U-Boot stay single-copy | The deck's risky boot settings (the S3's UART, the DSI panel, the watchdog) live in `config.txt` and overlays. With U-Boot they'd be updated **without** a fallback |
| **Attempts** | One per trial; a power-off cancels it | Several (3 in the demo), counted across power cycles | Minor: a cancelled trial only means installing again |
| **Fallback after commit** | None: "The firmware cannot fallback to the other slot if the primary slot gets unbootable" (Rtone) | Yes: the counters keep running, so a slot that breaks later falls back | **U-Boot's real advantage.** It matters if a committed slot breaks later, from SD corruption or a fault that only shows on a later boot |
| **Writes at every boot** | None; one FAT write per commit | `saveenv` to a non-redundant `uboot.env` on FAT at every boot. A redundant environment on a raw partition fixes that, with more setup | The decks were hardened to stop routine card writes |
| **Watchdog during boot** | The firmware arms it (`kernel_watchdog_timeout`; EEPROM boot watchdog on Pi 4 since 2025-07) | "U-Boot does not currently support the watchdog timer for the Raspberry Pi family … must boot … in less than apx. 16 seconds" (br2rauc) | With tryboot, a hang during start-up resets |
| **Boot time** | Nothing added | A stage after the firmware: about +0.5–1.5 s tuned, +3–5 s with defaults, 10–15 s in a known countdown bug (forum and IPFire reports; no controlled Pi 4 benchmark found) | The decks count seconds (splash at ~8 s) |
| **Pi OS fit** | Stock boot chain | Adds U-Boot, its script, environment and tools, none of which Pi OS uses | Sam prefers stock Pi OS |
| **RAUC integration** | Custom backend (~60 lines) now; native backend in review (rauc #1599) | Built-in backend, plus a ~45-line U-Boot boot script and `fw_env.config` | About the same amount of our own code |
| **pi-qemu** | Already plays the firmware; tryboot is our own C++ (§4.2) | U-Boot would have to run on QEMU's raspi4b. **Unverified** | Only tryboot is sure to be testable in the emulator |
| **Direction** | Raspberry Pi docs ("for fail-safe OS updates"), rpi-image-gen, Ubuntu 25.10, HAOS on Pi 5 since 2024, meta-rauc PR #181, RAUC #1599 | HAOS on Pi 2/3/4 (one boot flow across all its boards), br2rauc, meta-rauc today (a "demo") | HAOS's reason doesn't apply: the decks are one board type |

**Device tree experience.** edi-pi (Matthias Lüscher) used U-Boot first. The
firmware "loads the device tree binary and modifies it", which made a robust
kernel and device tree update hard. After moving to tryboot, they update the
kernel, the device tree with its overlays, `start4.elf`, `config.txt` and
`cmdline.txt` safely: "the boot happens without U-Boot".

**What tryboot costs us.** If a committed slot later becomes unbootable, the
deck doesn't heal itself. The mitigations:
- commit only after a full health check on a real start (§3.5);
- the firmware's watchdog-partition options might send a reset to the other
  slot. **Unverified**; checked on the bench (§7).
- Otherwise, the cure is a reflash.

**Reconsider U-Boot if:**
- decks must heal themselves unattended from a slot that breaks after commit,
  and the firmware's watchdog options don't do it;
- or several board types need one boot flow, as for HAOS.

---

## Sources

**Raspberry Pi**
- `config.txt` (autoboot.txt, `[tryboot]`, `[boot_partition=N]`, `[partition=N]`, `cmdline`): https://www.raspberrypi.com/documentation/computers/config_txt.html
- Bootloader, tryboot, models, EEPROM write-protect: https://www.raspberrypi.com/documentation/computers/raspberry-pi.html
- EEPROM release notes (Pi 4): https://github.com/raspberrypi/rpi-eeprom/blob/master/firmware-2711/release-notes.md
- GPT and boot partitions on a Pi 4: https://github.com/raspberrypi/rpi-eeprom/issues/654
- `recovery.bin` on a boot slot: https://github.com/raspberrypi/rpi-eeprom/issues/499
- `boot_partition` in start4.elf: https://github.com/raspberrypi/firmware/issues/1940
- Pi OS kernel config: https://github.com/raspberrypi/linux/blob/rpi-6.18.y/arch/arm64/configs/bcm2711_defconfig
- rpi-image-gen (`image-rota`): https://github.com/raspberrypi/rpi-image-gen

**RAUC**
- Docs: https://rauc.readthedocs.io/en/latest/ and https://rauc.readthedocs.io/en/v1.13/. The pages used:
  - basic: bundles, signing, streaming;
  - integration: system.conf, slot detection, custom backend, systemd, kernel
    options;
  - reference: system.conf keys, slot types, manifest, handlers, slot status;
  - advanced: PKI, adaptive updates;
  - faq;
  - changes.
- Native Pi backend (open): https://github.com/rauc/rauc/pull/1599
- meta-rauc-raspberrypi: https://github.com/rauc/meta-rauc-community/tree/master/meta-rauc-raspberrypi
- Its tryboot draft: https://github.com/rauc/meta-rauc-community/pull/181

**Integrations**
- Rtone backend: https://github.com/Rtone/raspberrypi-firmware-rauc-bootloader-backend
- rasputin-os: https://github.com/geekdojo/rasputin-os
- Home Assistant OS, Pi 5 tryboot: https://github.com/home-assistant/operating-system/blob/dev/buildroot-external/board/raspberrypi/rpi5-64/rootfs-overlay/usr/lib/rauc/rpi-tryboot.sh
- Bootlin, RAUC on a Pi 5: https://bootlin.com/blog/safe-updates-using-rauc-on-raspberry-pi-5/

**U-Boot or tryboot**
- br2rauc (U-Boot; no Pi watchdog in U-Boot): https://github.com/cdsteinkuehler/br2rauc
- meta-rauc's U-Boot boot script: https://github.com/rauc/meta-rauc-community/blob/master/meta-rauc-raspberrypi/recipes-bsp/rpi-u-boot-scr/files/boot.cmd.in
- edi-pi, from U-Boot to tryboot:
  - https://github.com/lueschem/edi-pi/issues/23
  - https://www.get-edi.io/Debian-Bullseye-Upgrade-for-the-Raspberry-Pi/
- Ubuntu 25.10, A/B boot with tryboot: https://discourse.ubuntu.com/t/call-for-testing-a-b-boot-on-raspberry-pi/64173
- Home Assistant OS, Pi 5:
  - https://github.com/home-assistant/operating-system/pull/2914
  - https://github.com/home-assistant/operating-system/discussions/2844
  - https://github.com/home-assistant/operating-system/pull/3842 (a stuck-bootloader bug)
- U-Boot boot time on a Pi 4 (reports, not benchmarks):
  - https://forums.raspberrypi.com/viewtopic.php?t=277010
  - https://community.ipfire.org/t/raspberry-pi-4-2s-u-boot-timeout-takes-15s/10822
- U-Boot in Debian: https://packages.debian.org/trixie/u-boot-rpi

**Debian**
- https://packages.debian.org/trixie/rauc
- https://packages.debian.org/trixie/arm64/rauc-service/filelist

**Checked in this repo and on the emulated deck** (trixie, 2026-09-15 image)
- `raspi-config`'s overlay code (`overlayroot=tmpfs`) and its Pi 5-only "AB
  Firmware" option.
- `overlayroot.conf`: tmpfs, `device:` and `crypt:` modes.
- Package versions available: overlayroot 0.18, rauc 1.13.
- The hardware watchdog (`RuntimeWatchdogSec=1min`), the `/tmp` and `/run`
  sizes, and `~/.mixxx` at 964 KB.
- `pi-qemu/app/src/firmware.cpp` and `machine.cpp`: no tryboot, and the
  filters they handle.
- QEMU's `bcm2835_property.c` (no reboot-flags message) and
  `bcm2835_powermgt.c` (keeps `RSTS`).
