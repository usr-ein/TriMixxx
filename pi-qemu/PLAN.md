# pi-qemu: the plan

pi-qemu runs a deck's SD card on an emulated Raspberry Pi 4: the emulated deck,
trimixxx0. That part works and is described in `README.md`. Part 2 of this file
records why it is built the way it is.

**What comes next** is how the decks run a locked system and take updates over
the air: A/B slots, chosen by the Pi firmware's own **tryboot** and installed by
**RAUC**, rehearsed on the emulated deck before any deck sees them. Part 1 is
that plan, phase by phase, written so that an agent can carry it out. Status on
2026-10-08: **phases 1 to 4 are done**: the firmware measured on trimixxx3
(§2.3), pi-qemu starting as it does (§3.5), a locked card (§4.3), and RAUC's
updates through the whole fault table on the emulated deck (§5.4). Phase 5
waits for trimixxx3 to be back on the bench; phase 6 is Sam's.

| Read | For |
|---|---|
| this file, Part 1 | **what to do, in order**: tasks, files, tests, stop points |
| `rauc-pi-4-report.md` | **how the system works**: diagrams as text, the update flow, the backend contract |
| `rauc-pi-4-setup.md` | **design and evidence**: every configuration file, the research, the sources (App. A–C) |
| `rauc-pi-4-report.html`, `rauc-pi-4-figures/` | the same report as a page and as images, for people |
| `README.md` | how to run pi-qemu and the emulated decks today |

---

# Part 1. A/B updates with RAUC and tryboot

## 1. Working on this plan

### 1.1 Ground rules

They come from the repo's `CLAUDE.md` and the `trimixxx0` skill:

- **Work from a git worktree.** Run `pi-qemu/worktree.sh prepare` first, and
  `pi-qemu/worktree.sh release` at the end.
- **Reach emulated decks only through `pi-qemu/instance.sh`**, under a name of
  your own. Never a bare `pi-qemu COMMAND`, and never an upload script on its
  own: without `HOST` they default to a real deck.
- **Real decks are off limits until phase 6:** `trimixxx-pi` (trimixxx1),
  `trimixxx-pi-2` / `trimixxx2`.
  - trimixxx3 is Sam's bench Pi. It's fine to use from phase 1 on.
- **Commits:** as `Samuel Prevost <usr_ein@pm.me>`. A change inside a submodule
  is committed there first, then bumped here. Never `git add -A`.
- **On MBP-NJ** (Sam's Mac), the emulated decks trust
  `~/.ssh/with_pass/rsa_sam`:
  - prefix `SSH_KEY=$HOME/.ssh/with_pass/rsa_sam` to `instance.sh up` and
    `image/build.sh`;
  - run `ssh-add -l` first. ssh-agent forgets the key at every reboot, and
    only Sam can load it again (it needs his passphrase).
- **Record what you learn.**
  - Results go into this file, in the phase's results table.
  - Answers to points marked **unverified** go into `rauc-pi-4-setup.md`.
  - A design change goes into both `rauc-pi-4-setup.md` and
    `rauc-pi-4-report.md`.

### 1.2 Stop points: ask Sam first

| What | Why |
|---|---|
| Flashing an SD card | Needs `sudo` (or Raspberry Pi Imager) and Sam's hands. Name the disk with `diskutil list external` and have Sam confirm it; never guess |
| Any hands-on step | Inserting a card, cabling, power-cycling trimixxx3 |
| Changing a board's EEPROM (`rpi-eeprom-update`, `rpi-eeprom-config --apply`) | Persistent hardware state |
| Replacing the shared golden snapshot (`instance.sh golden`), or rebuilding `.cache/build/trimixxx0.img` | Every agent's emulated deck starts from them |
| A hang test on the **committed** slot (phase 1, T9) | It can loop the Pi until the card is fixed on the Mac |
| Anything on a real deck, and phase 6 as a whole | Gig equipment |
| Pushing | Each phase once it's done, then go on (Sam, 2026-10-08) |

### 1.3 Invariants

Everything built must keep these true. They are what makes updates safe.

1. **The running slot is never written.** Only the idle slot's boot and
   system partitions are written, and only by RAUC.
2. **`autoboot.txt` is the only file written outside RAUC's slots.**
   - It is always generated whole, never edited.
   - It is written to a temporary file, `fsync`ed, renamed over the old one,
     and the directory is synced.
   - Only the backend writes it.
3. **A slot becomes the default (`[all] boot_partition`) only through
   `rauc status mark-good`, on a trial start of that slot,** after the health
   check passed. The device tree's `tryboot` reads 1 on such a start.
4. **The health check acts only on a trial start, and runs after
   NetworkManager and sshd.** On failure it runs `rauc status mark-bad`, then
   reboots. It can never stop the committed slot from starting.
5. **Both boot slots hold byte-identical files.** `config.txt`'s
   `[boot_partition=N]` picks `cmdline-a.txt` or `cmdline-b.txt`; nothing is
   patched at install time.
6. **Each slot's command line carries `rauc.slot=A` or `rauc.slot=B`.** It never
   carries `resize`, or a first-boot `init=` hook.
7. **The card's MBR disk signature is fixed and never changes.**
8. **Nothing per-deck lives in the slots.** Identity lives on p7 (`/data`,
   read-only) and is applied at start by the identity unit.
9. **RAUC's data directory is p8 (`/var/lib/rauc`, read-write).**
10. **Bundles are `format=plain`** while the image carries RAUC 1.13.
11. **pi-qemu copies only firmware behaviour that is documented or was measured
    on trimixxx3.** Where the two disagree, the hardware wins.
12. **The dev card stays writable.** The release-only switches, all made by
    `make release`'s seal, are:
    - `overlayroot=tmpfs` and the slot command lines;
    - the `/data` and `/var/lib/rauc` mounts (`nofail`);
    - RAUC's configuration;
    - the identity and health units, enabled.

    A dev deck carries the same files, installed but inert.
13. **p1 holds `autoboot.txt` and no firmware: never a `start*.elf`.** A
    missing, empty or garbled `autoboot.txt` then starts p2 (phase 1, T6c and
    T7). With firmware files on p1, the same damage can leave a deck dark
    (T6b).
14. **Every board runs a current bootloader before its first A/B card:**
    2026-05-17 on trimixxx3. One from before 2022-12-01 doesn't know
    `tryboot_a_b`, reads `autoboot.txt` without its sections, and starts slot
    B (T0). Its EEPROM also sets the boot watchdog (Sam, 2026-10-08):
    `BOOT_WATCHDOG_TIMEOUT=45`, `BOOT_WATCHDOG_PARTITION=2`.

### 1.4 The target, in one page

The card is MBR, with the fixed disk signature `0x5d0bc1ec` (the first 32 bits
of sha256("trimixxx"), set in `release/Makefile`), so `<id>` below is
`5d0bc1ec`:

| # | Label | FS | Size | Holds |
|---|---|---|---|---|
| p1 | bootsel | FAT32 | 64 MiB | `autoboot.txt` only (invariant 13) |
| p2 | boot-A | FAT32 | 512 MiB | firmware, kernel, initramfs, DTBs, overlays, `config.txt`, `cmdline-a.txt`, `cmdline-b.txt` |
| p3 | boot-B | FAT32 | 512 MiB | the same files |
| p4 | extended | | | p5–p8 |
| p5 | rootfs-A | SquashFS (RAUC `type=raw`) | 4 GiB | the system |
| p6 | rootfs-B | SquashFS | 4 GiB | the next system |
| p7 | trimixxx-data | ext4 | 64 MiB | `trimixxx.conf` (deck name), `NetworkManager/*.nmconnection`, `ssh/ssh_host_*` |
| p8 | trimixxx-state | ext4 | 4 GiB | RAUC's data directory, bundles, the health check's log |

The card ends with p8, at about 13 GiB (a 14.1 GB image), so it flashes onto
any card of 16 GB or more: the decks' 32 GB cards and trimixxx3's 64 GB one
take the same image. The rest of a bigger card stays unused, since nothing
ever grows the card (invariant 6).

The exact files are in `rauc-pi-4-setup.md` §3: `autoboot.txt`, the
`config.txt` additions, the command lines (§3.2), `system.conf` and the
manifest (§3.5).

### 1.5 Where things go

These paths are proposals; keep them unless there's a reason not to.

| Path | What |
|---|---|
| `pi-qemu/release/Dockerfile` | debian:trixie with genimage, rauc, squashfs-tools, dosfstools, mtools, e2fsprogs, fdisk, zstd, xz-utils, openssl, make. **Built** |
| `pi-qemu/release/genimage-phase1.cfg`, `pi-qemu/release/phase1/` | phase 1's test card, and its files. **Built** |
| `pi-qemu/release/genimage.cfg` | the release card, and the boot, bootsel, data and state images |
| `pi-qemu/release/boot/` | `autoboot.txt` (**built**, shared with phase 1), `config-release.txt` (the additions), `cmdline-a.txt.in`, `cmdline-b.txt.in` |
| `pi-qemu/release/exclude.txt` | the seal: what a release leaves out of the system |
| `pi-qemu/release/manifest.raucm.in` | the bundle manifest |
| `pi-qemu/release/Makefile` | `phase1`, `release`, `card`, `ship`, `faults` (**built**); run as `make -C pi-qemu/release …` (`make release` in the docs). Each target builds the container, then runs again inside it |
| `pi-qemu/release/signing-key.pem` | the key that signs bundles, public on purpose (§5.1) |
| `pi-qemu/release/out/<v>/` | outputs, gitignored: `trimixxx-<v>.img`, `trimixxx-<v>.raucb`, `manifest.txt` |
| `pi-qemu/.cache/decks/<deck>/` | per-deck secrets, gitignored: `trimixxx.conf`, `NetworkManager/`, `ssh/` |
| `pi_config/rauc/` | `system.conf`, `keyring.pem` (the certificate), `rpi-tryboot` (backend) |
| `pi_config/trimixxx-health.service`, `pi_config/trimixxx-health` | the health unit and its checks |
| `pi_config/trimixxx-identity.service`, `pi_config/trimixxx-identity` | the identity unit |
| `pi_config/eth0-link-local.nmconnection` | eth0's profile on every deck, replacing cloud-init's netplan one (phase 3); phase 1's card already uses it |
| `pi-qemu/deploy/base.sh` | installs `overlayroot`, `rauc` and `rauc-service`; adds the initramfs modules |
| `pi-qemu/app/src/firmware.{h,cpp}`, `machine.{h,cpp}` | tryboot in pi-qemu |
| `pi-qemu/qemu/trimixxx-patches.py` | the QEMU patch for the firmware's reboot flags |
| `pi-qemu/instance.sh` | `up --from` accepts cards of any size |

---

## 2. Phase 1: firmware facts on trimixxx3

**Goal.** Measure, on real hardware, every firmware behaviour the design rests
on and the documentation leaves open:
- whether `[boot_partition=N]` works on a Pi 4;
- tryboot, armed both ways;
- power-off;
- a missing or broken `autoboot.txt`;
- the watchdog during a trial;
- whether a hang on the committed slot can fall back.

pi-qemu then copies what was measured (phase 2).

**What Sam provides:**
- The spare Pi 4. It has no screen; that's fine.
- An SD card of 8 GB or more. It gets erased.
- An Ethernet cable from the Pi to the Mac.
- Power cycles on request.
- Optional: a 3.3 V USB-serial adapter on GPIO 14 (TX), 15 (RX) and GND,
  which shows the bootloader's own log.

### 2.1 The test card (built)

```sh
SSH_KEY=$HOME/.ssh/with_pass/rsa_sam make -C pi-qemu/release phase1   # on MBP-NJ; about 15 s
```

That writes `pi-qemu/release/out/phase1/trimixxx3-phase1.img` (5.26 GiB), the
release layout with one Linux root shared by every slot. The slot that started
is told apart by `rauc.slot=` on `/proc/cmdline` and by the device tree.

| # | FS, label | Size | Holds |
|---|---|---|---|
| p1 | FAT32 `BOOTSEL` | 256 MiB | `autoboot.txt`, **plus Pi OS's boot files, `cmdline-r.txt`, `cmdline.txt` (`R-nofilter`) and the seed**. If the bootloader ignores `autoboot.txt` (too old), the Pi still starts from p1, as `rauc.slot=R`, and can be reached |
| p2 | FAT32 `bootfs` | 512 MiB | Pi OS's boot files, `config.txt`, `cmdline-a.txt`, `cmdline-b.txt`, `cmdline.txt` (`A-nofilter`), the seed |
| p3 | FAT32 `bootfs-b` | 512 MiB | the same, except `cmdline.txt` (`B-nofilter`) |
| p4 | extended | | |
| p5 | ext4 `rootfs` | 4 GiB | Pi OS's root |

`release/Makefile` makes it from the pinned stock image
(`pi-qemu/.cache/2026-09-15-raspios-trixie-arm64-lite.img`), in the release
container, without privileges:
- **Boot files:** Pi OS's, copied out with `mcopy`. Its `cmdline.txt` is left
  out: it ends in `resize`, which grows the root and rewrites the disk
  signature.
- **`config.txt`:** Pi OS's, plus `release/phase1/config.txt`:
  ```
  [boot_partition=1]
  cmdline=cmdline-r.txt
  [boot_partition=2]
  cmdline=cmdline-a.txt
  [boot_partition=3]
  cmdline=cmdline-b.txt
  [all]
  dtoverlay=disable-bt
  enable_uart=1
  ```
  `disable-bt` puts the PL011 on GPIO 14/15, as on the decks. pi-qemu gives a
  card its console socket (`instance.sh console NAME COMMAND`) only when
  `serial0` is the PL011.
- **Command lines.** They differ only in the slot they name and the boot
  partition they mount: `cmdline-b.txt` has `B` and `-03`, `cmdline-r.txt`
  has `R` and `-01`, and the `cmdline.txt` files have `A-nofilter`,
  `B-nofilter` and `R-nofilter`.
  ```
  console=serial0,115200 console=tty1 root=PARTUUID=5d0bc1ec-05 rootfstype=ext4 fsck.repair=yes rootwait rauc.slot=A systemd.mount-extra=PARTUUID=5d0bc1ec-02:/boot/firmware:vfat
  ```
- **The seed.** Pi OS reads cloud-init's seed from `/boot/firmware`
  (`seedfrom: file:///boot/firmware`, and cloud-init waits for that mount), so
  every boot partition carries it:
  - Pi OS's own `meta-data` and `network-config`, which is empty;
  - `release/phase1/user-data.in`: hostname `trimixxx3`, and user `sam1902`
    as `image/build.sh` makes it (groups, the `SSH_KEY`, passwordless sudo,
    the password hash from `image/secrets.env`). It turns ssh on, then creates
    `/etc/cloud/cloud-init.disabled`, so later starts skip cloud-init.
- **The root:** copied with `dd`, checked with `e2fsck`, grown to 4 GiB with
  `resize2fs`, then edited with `debugfs`:
  - `/etc/fstab` holds `proc` and
    `PARTUUID=5d0bc1ec-05 / ext4 defaults,noatime 0 1`. `/boot/firmware`
    comes from `systemd.mount-extra=`.
  - Two NetworkManager keyfiles from `release/phase1/`, owned by root, mode
    0600, in `/etc/NetworkManager/system-connections/`:
    - `eth0-link-local`: eth0, link-local IPv4 and IPv6, like the decks'
      eth0;
    - `pi-qemu-home`: DHCP on `usb0`, pi-qemu's management NIC. A Pi has no
      `usb0`.
  - `rpi-eeprom-update.service` is masked. Pi OS runs
    `rpi-eeprom-update -s -a` at every start, which would stage an EEPROM
    update without asking (§1.2).
- **genimage** (`genimage-phase1.cfg`): MBR, disk signature `0x5d0bc1ec`, p1
  at 8 MiB, 4 MiB alignment, p4 extended, p5 logical.

**What building it taught:**
- **Netplan can't express "DHCP, else link-local".** `dhcp4: true` with
  `link-local: [ipv4, ipv6]` renders `ipv4.method=auto` with no fallback. On a
  cable with no DHCP server, NetworkManager then leaves eth0 without IPv4.
- **Pi OS's NetworkManager rewrites netplan's profiles.** cloud-init's eth0
  profile came back as `/etc/netplan/90-NM-*.yaml` with `match: {}`, which
  takes whichever NIC appears first. `image/build.sh` met the same rewrite.
  Keyfiles in `/etc/NetworkManager` are NetworkManager's own and are left
  alone. The release's identity unit uses keyfiles too.
- **NetworkManager's automatic profile for `usb0` is link-local**, not DHCP.
  That's why every emulated deck carries `pi-qemu-home`.
- **cloud-init leaves the network alone when it's given no config.** Pi OS
  sets `disable_fallback_netcfg: true`.
- **Pi OS's journal is volatile**, so `journalctl --list-boots` shows one boot.
  The helpers below use `boot_id`.
- **Pi OS makes a swap file at first start.** That leaves 940 MB free on the
  4 GiB root.

**Smoke test in pi-qemu: passed on 2026-10-08,** with a copy rounded up to
8 GiB and `instance.sh up p1 --from …`:
- **Timing:** the first start reached ssh in 8 s, cloud-init included. The
  second start took 3.8 s.
- **Slot:** `rauc.slot=A-nofilter`, as expected: pi-qemu doesn't know
  `[boot_partition=N]` until phase 2.
- **System:** `/boot/firmware` is p2; the hostname is `trimixxx3`; sudo, ssh
  and avahi work.
- **Network:** eth0 has `169.254.207.237`, and `usb0` has QEMU's `10.0.2.15`.
  `/etc/netplan` stays empty.
- **Health:** no unit failed, and `rpi-eeprom-update` stays masked.

### 2.2 Flash and connect (Sam)

Sam flashes the card. The agent runs no disk command. On the Mac:

1. **Find the card.** Put it in, then run `diskutil list external physical`.
   The card is the `/dev/diskN` of its size. Check it twice: `dd` erases
   whatever disk it's given.
2. **Write the image:**
   ```sh
   diskutil unmountDisk /dev/diskN
   sudo dd if=$HOME/Documents/CustomDJ/pi-qemu/release/out/phase1/trimixxx3-phase1.img of=/dev/rdiskN bs=4m status=progress
   diskutil eject /dev/diskN
   ```
   - After `dd`, macOS mounts the card's three FAT volumes. It may also say it
     can't read the fourth (the ext4 root): choose "Ignore", then eject.
   - Raspberry Pi Imager works too ("Use custom"). Answer **No** to OS
     customisation: it would write its own seed onto p1.
3. **Cable** the Pi's Ethernet to the Mac. With no DHCP server, both sides take
   link-local addresses, and avahi announces `trimixxx3.local`. The Mac's
   Wi-Fi can stay on.
4. **Power on.** The first start takes 1–2 minutes: cloud-init, host keys, the
   swap file.
5. **Connect:** `ssh -o StrictHostKeyChecking=accept-new sam1902@trimixxx3.local`.
   The key must be in the agent.

**If `trimixxx3.local` doesn't answer:**
- run `dns-sd -G v4v6 trimixxx3.local`;
- or `ping6 -c2 ff02::1%<the Mac's Ethernet interface>` to find the Pi's
  `fe80::` address;
- or use the console: a 3.3 V USB-serial adapter on GPIO 14 (TX), 15 (RX) and
  GND, at 115200 8N1, logging in as `sam1902` with the password.

macOS Internet Sharing doesn't help here: eth0 is link-local only and never
asks for DHCP.

### 2.3 The tests

On the Pi, define:
```sh
dt() { od -An -tu4 --endian=big "/proc/device-tree/chosen/bootloader/$1" 2>/dev/null | tr -d ' '; }
where() { echo "slot=$(grep -o 'rauc.slot=[^ ]*' /proc/cmdline | cut -d= -f2) partition=$(dt partition) tryboot=$(dt tryboot) boot=$(cut -c1-8 /proc/sys/kernel/random/boot_id)"; }
```

| # | Steps | Expected |
|---|---|---|
| T0 | First start | `slot=A partition=2 tryboot=0`. Record: `vcgencmd bootloader_version`, `vcgencmd bootloader_config`, `ls /proc/device-tree/chosen/bootloader/` with every value (`xxd` each file), `sudo vclog --msg`, `cat /proc/cmdline` |
| T0b | If T0 shows `slot=R`, or a bootloader older than 2022-12-01 | **Stop, ask Sam.** Update the EEPROM from the running system (`sudo rpi-eeprom-update -a`, then a reboot) and run T0 again. If T0 shows `A-nofilter`, `[boot_partition=N]` isn't honoured: record it, and go to "If a result breaks the design" |
| T1 | `sudo systemctl reboot --reboot-argument="0 tryboot"` (trixie's systemd refuses `reboot '0 tryboot'`) | `slot=B partition=3 tryboot=1` |
| T2 | `sudo reboot` | `slot=A partition=2 tryboot=0` |
| T3 | `sudo vcmailbox 0x00038064 4 0 1`; read it back with `sudo vcmailbox 0x00030064 4 0 0`; then `sudo reboot` | `slot=B tryboot=1` |
| T4 | Arm (as in T3), `sudo poweroff`, Sam power-cycles | `slot=A` (the flag doesn't survive power-off) |
| T5 | During a trial (`slot=B`), Sam power-cycles | `slot=A` |
| T6 | From A: mount p1 and rename `autoboot.txt`, then reboot. Then also rename p1's `start4.elf`, and reboot again. Restore both afterwards | First reboot: partition 0 means the first bootable FAT, so expect `slot=R` (p1 is bootable). Second: `partition=2` (the walk). Record both |
| T7 | `autoboot.txt` made empty, then filled with garbage; reboot after each; restore | Record what starts |
| T8 | Add `kernel_watchdog_timeout=60` to `config.txt` on p2 and p3 and `break=premount` to `cmdline-b.txt`, then `sudo systemctl reboot --reboot-argument="0 tryboot"`. Optional, without `kernel_watchdog_timeout`, which needs a power cycle to end | B hangs in the initramfs; the watchdog resets it within about 90 s; `slot=A`. Without the setting, it's expected to hang forever, because Pi OS's initramfs disarms the watchdog. Remove the edits afterwards |
| T9 | **Ask Sam first.** In p2's `config.txt`, under `[boot_partition=2]`: `kernel_watchdog_timeout=60` and `kernel_watchdog_partition=3`. Add `break=premount` to `cmdline-a.txt`, then reboot | If B starts (`partition=3 tryboot=0`), the firmware can fall back after a commit. If A keeps looping, Sam takes the card out and removes `break=premount` from `cmdline-a.txt` on the Mac (the FAT volume `bootfs`) |
| T10 | Optional, ask Sam: EEPROM `BOOT_WATCHDOG_TIMEOUT` / `BOOT_WATCHDOG_PARTITION` | Only if T9 failed |
| T11 | Time from power-on to ssh, for A and for a trial of B | Record |

**Results**, measured on trimixxx3 on 2026-10-08:

| Test | Observed | Notes |
|---|---|---|
| T0 | **Bootloader 2021-04-29: `slot=B partition=3 tryboot=0`.** After T0b, on 2026-05-17: `slot=A partition=2 tryboot=0` | The 2021 bootloader reads `autoboot.txt` but not its sections, so the last `boot_partition=` (3) wins. The firmware honours `[boot_partition=N]`: it read `cmdline-b.txt`, then `cmdline-a.txt` (`vclog`: `boot-part: 3`, then `2`). Board: Pi 4B rev 1.2, 4 GB |
| T0b | Done with Sam's OK. Bootloader 2021-04-29 → 2026-05-17 (capabilities `0x1f` → `0x7f`), VL805 `138a1` → `138c0`; one reboot, 46 s | The ROM runs `recovery.bin` from p1. `rpi-eeprom-update -a` stages it on `/boot/firmware`, which is p2 or p3 on this card, so it was staged with `sudo mount /dev/mmcblk0p1 /mnt; sudo BOOTFS=/mnt rpi-eeprom-update -a`. It renamed itself `RECOVERY.000`. `BOOTFS=/mnt rpi-eeprom-update -r` then cleared p1 |
| T1 | `slot=B partition=3 tryboot=1`, back in 39 s | `sudo reboot '0 tryboot'` fails on trixie: systemd 257 says "Too many arguments". `--reboot-argument=` works |
| T2 | `slot=A partition=2 tryboot=0`, 38 s | The flag lasts one start |
| T3 | `SET_REBOOT_FLAGS 1` read back as `1`; then a plain reboot gives `slot=B partition=3 tryboot=1`, 38 s | Arming through the mailbox works: the backend's way |
| T4 | Armed (read back `1`), `systemctl poweroff`, power pulled and restored: `slot=A partition=2 tryboot=0` | The flag doesn't survive a power-off |
| T5 | Power pulled during a B trial: back on `slot=A partition=2 tryboot=0` | `rsts` reads `00001000` after a cold start too, so it doesn't tell a power-on from a reboot. Linux took 11.4 s (kernel 3.0 s, userspace 8.3 s) |
| T6 | No `autoboot.txt`: `slot=R partition=1 tryboot=0`, p1 at `/boot/firmware`. Then no `start4.elf` on p1 either: **no start at all**, a black screen; restored on the Mac | **Breaks the design** (§A.4 of `rauc-pi-4-setup.md`). With no `autoboot.txt` the bootloader uses partition 0, the first FAT partition, and `PARTITION_WALK` only searches when a partition was requested. A release card's p1 holds only `autoboot.txt`, so losing it would leave a deck unbootable. **T6c settles it** (below) |
| T6c | p1 like the release's: no `start*.elf` at all, no `autoboot.txt`: **`slot=A partition=2 tryboot=0`**, in 37 s | The design holds. Partition 0 resolves to the first FAT partition with firmware. T6b failed because p1 still held other `start*.elf` files, so it looked bootable. Rule: **a release's p1 holds no `start*.elf`** (it holds only `autoboot.txt`). No EEPROM setting needed |
| T7 | On a release-like p1 (no `start*.elf`): an empty `autoboot.txt` gives `slot=A partition=2 tryboot=0`, and so do 512 random bytes | A damaged `autoboot.txt` (missing, empty or garbage) falls back to A |
| T8 | A B trial with `break=premount`, and `kernel_watchdog_timeout=60` on p2 and p3: hung at `(initramfs)`, the watchdog reset the board, back on `slot=A partition=2 tryboot=0` 118 s after the reboot command | The firmware passes `watchdog.open_timeout=60`, so Pi OS's initramfs script `rpi_wd` leaves the watchdog armed. Without `kernel_watchdog_timeout` it disarms it (read in the script, not run). Once up, systemd holds `/dev/watchdog0`. `rsts` still reads `00001000` |
| T9 | `kernel_watchdog_timeout=60` and `kernel_watchdog_partition=3` in A's `[boot_partition=2]`, and `break=premount` for A: the watchdog fired about 60 s into the hang, and **A started again** and hung again, a loop. Fixed on the Mac | The documented `kernel_watchdog_partition` didn't send the reset to B. The firmware doesn't rescue a committed slot that hangs after Linux starts, so phase 4 adds no reconcile step for it |
| T10 | EEPROM `BOOT_WATCHDOG_TIMEOUT=45` and `BOOT_WATCHDOG_PARTITION=3`. A's `start4.elf` truncated to 64 KiB: the boot watchdog reset the board after 45 s and **B started**, `slot=B partition=3 tryboot=0`, at +82 s | The boot watchdog covers failures before Linux starts. A `kernel=` naming a missing file is no such failure: the firmware falls back to `kernel8.img`. Its partition is a fixed number, not "the other slot" (open question in `rauc-pi-4-setup.md` §9). The EEPROM config is back to its defaults |
| T11 | Reboot command to ssh: 38 s for A, 39 s for a B trial. From reset: 15.7 s in the firmware before Linux starts, kernel 2.5 s, userspace 8.3 s | A trial adds no time. The firmware spends its time on HDMI probing and SD reads |

Also record the raw `/proc/device-tree/chosen/bootloader/` dump for a normal
start and for a trial. pi-qemu copies those property names and values. Measured
on bootloader 2026-05-17 (hex, as stored):

| Property | Normal start (A) | Trial (B) |
|---|---|---|
| `boot-mode` | `00000001` | `00000001` |
| `build-timestamp` | `6a0a134e` | `6a0a134e` |
| `capabilities` | `0000007f` | `0000007f` |
| `name` | `"bootloader"` | `"bootloader"` |
| `partition` | `00000002` | `00000003` |
| `rsts` | `00001000` (`00001020` on the start right after the EEPROM update) | `00001000` |
| `tryboot` | `00000000` | `00000001` |
| `update-timestamp` | `6aa88f02` | `6aa88f02` |
| `version` | `"224877da90f82a72dbcc9db10bcf059259f54680"` | the same |

**If a result breaks the design:**
- **`[boot_partition=N]` isn't honoured.** Fall back to RAUC's documented
  slot post-install hook, writing the slot's `cmdline.txt` (as Rtone's backend
  does). Invariant 5 then changes; update `rauc-pi-4-setup.md` §3.2.
- **The watchdog doesn't cover a trial.** Find out why before going on: the
  watchdog is what turns a hung trial into a fallback.
- **T9 works.** Phase 4 adds the reconcile step (§5.2).

**Outcome:**
- `[boot_partition=N]` is honoured (T0).
- The watchdog covers a trial (T8).
- T9 didn't work, so phase 4 adds no reconcile step.

What did change the design:
- T0: the bootloader requirement, invariant 14;
- T6b and T6c: what p1 may hold, invariant 13;
- T6 and T7: `repair` can't trust the running slot (§5.2).

**Done when** every row has an observed result, the unverified points in
`rauc-pi-4-setup.md` App. A are settled, and any design change is written down.

---

## 3. Phase 2: tryboot in pi-qemu

**Goal.** The emulated deck reproduces phase 1's results, so updates can be
rehearsed on the Mac.

### 3.1 QEMU patch (`qemu/trimixxx-patches.py`, then `qemu/build.sh`)

- **`hw/misc/bcm2835_property.c`:**
  - Handle the firmware message `0x00038064` (SET_REBOOT_FLAGS): store the
    value in a new `uint32_t reboot_flags` in `BCM2835PropertyState`.
  - Handle `0x00030064` (GET_REBOOT_FLAGS) by returning it.
  - Today both fall through to "unimplemented".
  - Linux sends the same message when asked to `reboot "0 tryboot"`, which is
    why phase 1's T1 and T3 behaved alike. On trixie that command is
    `systemctl reboot --reboot-argument="0 tryboot"`.
- **Expose `reboot_flags` as a QOM property** (`reboot-flags`) so QMP's
  `qom-get` can read it.
- **For snapshots,** put it in a VMState *subsection* whose `.needed` returns
  `reboot_flags != 0`. Existing golden snapshots then still restore.
- **`hw/misc/bcm2835_powermgt.c`:** expose `rsts` as a read-only QOM property.
  The partition the kernel asks for (`reboot N`) is in it: partition bit *i*
  is RSTS bit *2i* (bits 0, 2, … 10), and 63 means halt (0x555).
- **Find both objects' QOM paths** with `qom-list` on a running instance, and
  record them in the patch's comment.

### 3.2 Machine (`app/src/machine.{h,cpp}`)

- **Replace `-no-reboot` with `-action reboot=shutdown,shutdown=pause`.**
- **On the QMP `SHUTDOWN` event with reason `guest-reset`:**
  - `qom-get` the reboot flags and `rsts`, then `quit`;
  - decode the requested partition;
  - power on again with `FirmwareOptions{tryboot = flags & 1, requestedPartition}`.
- **On `guest-shutdown`:** `quit`, then `poweredOff`, as today.
- **A power-on from the panel or CLI** passes no flag and no requested
  partition: a power cycle forgets both, as on a Pi.
- **The restore path is unchanged:** a saved machine is only used at first
  power-on.

### 3.3 Firmware step (`app/src/firmware.{h,cpp}`)

- **Filters.**
  - `[boot_partition=N]` is true when `config.txt` is read from partition N.
  - `[partition=N]` is true when the requested partition is N.
  - `[tryboot]` stays as it is.
  - Any other filter is false (`[0x<serial>]` too, unless an emulated serial
    is added).
  - Check the AND rules against the documentation: filters of different kinds
    AND together, and `[all]` resets.
- **Choosing the boot partition**, as phase 1 measured it:
  - A requested partition (`reboot N`) wins.
  - Otherwise `autoboot.txt`'s `boot_partition` applies, from `[all]`,
    overridden by `[tryboot]` when the flag is set (T1–T3).
  - A missing, empty or garbled `autoboot.txt` means partition 0: the first
    FAT partition holding a `start.elf`, the documentation's test for
    bootable. Phase 1's p1 is one (T6a, `slot=R`); the release's isn't (T6c
    and T7 start p2).
  - A partition chosen that way that has no `start4.elf` starts nothing: no
    kernel, a black screen (T6b). The real bootloader then cycles through its
    other boot devices.
  - A *requested* partition with no `start4.elf` is passed over for the next
    one that has it (`PARTITION_WALK`, on by default). That's documented, not
    measured.
- **`kernel=`** naming a missing file falls back to `kernel8.img` (T10).
- **With the flag set and no `tryboot_a_b=1`,** read `tryboot.txt` instead of
  `config.txt`.
- **Device tree:** create `/chosen/bootloader` with the nine properties and
  values in §2.3's table. Numbers are 32-bit big-endian cells
  (`fdtput -t u`), and `name` and `version` are strings.
  - `boot-mode` is 1 and `capabilities` is `0x7f`.
  - `partition` and `tryboot` are as chosen.
  - `rsts` read `0x1000` after a reboot, a power-on and a watchdog reset
    alike, so a constant will do.
- **Watchdog**, as phase 1 measured it:
  - `kernel_watchdog_timeout=N` in `config.txt` appends
    `watchdog.open_timeout=N` to the command line, as the firmware does
    (T8).
  - Pi OS's initramfs script `rpi_wd` then leaves the watchdog armed, and
    QEMU's BCM2835 watchdog resets a guest that hangs before systemd starts.
    Without the setting, `rpi_wd` disarms the watchdog.
  - That reset is a plain guest reset with no flag, so A starts (T8).
  - `kernel_watchdog_partition` changes nothing (T9).
  - The EEPROM's boot watchdog (T10) acts in firmware that pi-qemu doesn't
    run, so it isn't emulated.

### 3.4 `instance.sh`

- `up NAME --from CARD` clones the card, then rounds the clone up to the next
  power of two with `truncate`. The file stays sparse.
- Plain `pi-qemu run` keeps its size check.

### 3.5 Tests

**Boot phase 1's card in pi-qemu**, and repeat T0–T9, except T0b and T10
(the EEPROM, not emulated):
- power cycles: `instance.sh ctl p1 power off`, then `power on`;
- while the guest hangs in its initramfs, ssh can't reach it, but
  `instance.sh console p1 …` can: the card carries `disable-bt`;
- results must match phase 1's table, or the difference is written down as an
  emulator limit.

**Regressions:**
- `instance.sh up x` still restores the golden snapshot in about 4 s;
- `instance.sh deploy x config` still works;
- the panel and `ctl` commands still work.

**Stop point:** only if the golden snapshot no longer restores does it need
remaking, and that's Sam's call (§1.2).

**Done when** phase 1's emulatable rows give the same results in pi-qemu.

**Done on 2026-10-08.** Phase 1's card, in pi-qemu:

| Test | pi-qemu | trimixxx3 |
|---|---|---|
| T0 | `slot=A partition=2 tryboot=0`; `/chosen/bootloader` holds the measured values | the same |
| T1–T3 | B (trial), A, B (trial); 10 s per reboot | the same; 38 s |
| T4, T5 | A | A |
| T6a, T6b | R; then nothing starts, and pi-qemu says why | R; then a black screen |
| T6c, T7 | A | A |
| T8 | A, 75 s after the reboot command | A, 118 s |
| T9 | A, in a loop | the same |
| T10 | not emulated: the EEPROM's watchdog acts in firmware pi-qemu doesn't run | B |

**Regressions.**
- The golden snapshot restores in 3 s with the patched QEMU, so it wasn't
  remade.
- A dev deck reboots in 16 s, through the new pause.
- `deploy config` and the `ctl` commands work.

**Found on the way.**
- On macOS 27 with Xcode 26.4, QEMU's configure needs Xcode's SDK
  (`SDKROOT`, now set in `qemu/build.sh`), as `app/build.sh` already knew.
- mtools' `mren` writes names that fit 8.3 in capitals (`START4.ELF`). FAT
  doesn't mind.

---

## 4. Phase 3: a locked card

**Goal.** From a tagged commit, `make release` builds a card that boots
read-only in pi-qemu, with its slots, `/data` and state partition. RAUC comes
in phase 4.

### 4.1 Changes to the deck's system

They go into the normal build, so the dev deck carries them too, inert.

- **`pi-qemu/deploy/base.sh`:**
  - install `overlayroot`, `rauc` and `rauc-service`;
  - add `squashfs` and `overlay` to `/etc/initramfs-tools/modules`, then run
    `update-initramfs -u`.
- **fstab entries**, in the release's own fstab (`release/rootfs/etc/fstab`,
  written by the seal), not the dev card's:
  ```
  LABEL=trimixxx-data   /data          ext4  ro,noatime,nofail  0 2
  LABEL=trimixxx-state  /var/lib/rauc  ext4  noatime,nofail     0 2
  ```
  On a dev card, which has neither partition, a `nofail` line still leaves a
  device job pending until its timeout, and a unit ordered after that mount
  (the identity unit, before NetworkManager) would wait for it. So the seal
  also enables the identity and health units; the system step installs them,
  inert.
- **The identity unit** (`trimixxx-identity.service`). It runs early:
  `After=local-fs.target`, `Before=NetworkManager.service ssh.service`. It
  does nothing when `/data/trimixxx.conf` is missing (the dev card). Otherwise
  it does the following, writing only into the RAM layer:
  - sets the hostname from `trimixxx.conf`;
  - copies `/data/NetworkManager/*.nmconnection` into
    `/run/NetworkManager/system-connections/`, mode 0600;
  - copies `/data/ssh/ssh_host_*` into `/etc/ssh/`;
  - links the deck's pre-rendered Mixxx files into `~/.mixxx`.

  The copies go into NetworkManager's volatile directory and sshd's default
  host key path, so neither NetworkManager nor sshd is reconfigured, and the
  dev card keeps its network and ssh.
- **eth0's profile becomes a keyfile.** `pi_config` ships
  `eth0-link-local.nmconnection`, as phase 1's card does: link-local, bound
  to `eth0`, the same on every deck, so it's part of the image.
  - Today eth0's profile is cloud-init's, rewritten by NetworkManager into
    `/etc/netplan/90-NM-*.yaml` with `match: {}`. That's measured on the dev
    card and on phase 1's card. It takes whichever Ethernet NIC comes up
    first, and `prolink-eth0.sh` then makes it link-local.
  - A dev-deck change: try it on an emulated deck first, with wlan0
    untouched.
- **Per-deck profiles move to the identity:**
  - **`trimixxx-hotspot`:** its SSID is the hostname, and its channel comes
    from a per-deck table in `wifi-fallback/install.sh`. `make card` writes it
    into `/data/NetworkManager/`, from the deck's name and a channel moved
    into `units/<deck>.json`.
  - **`pi-qemu-home`** (DHCP on `usb0`) belongs to trimixxx0. Without it the
    emulated deck has no ssh, because NetworkManager's own profile for `usb0`
    is link-local (phase 1).
- **Pre-render every deck.** `mixxx_config/upload.sh` renders each
  `units/<deck>.json` (wiring, accent) into the image, under
  `/usr/share/trimixxx/decks/<deck>/`, instead of only the build host's.
- **Core dumps:** nothing to do. Pi OS lite has no `systemd-coredump`, and the
  core size limit is 0.
- **Swap: zram only.** Change `pi_config/trimixxx-swap-sizes.conf`. A swap file
  on a read-only root would land in RAM. Ask Sam whether the dev deck may lose
  its swap file too (one shared setting), or the release sets it alone. Pi
  OS's swap file is `/var/swap`, 2 GiB on the dev card; the release leaves it
  out (§4.2).
- **Mask `rpi-eeprom-update.service`.** EEPROM updates become a deliberate
  step (phase 6).
- **The boot splash shows the slot** (Sam's request, 2026-10-08). The logo
  (`pi_config/trimixxx-splash.*`) gets a label under it:
  - `A` or `B`;
  - `trial` while a new release is on probation (the device tree's `tryboot`
    is 1);
  - the release's version.

  It's the splash's own way, with nothing new on the deck:
  - `splash-render.py` pre-renders one blob per label: `A`, `B`, `A trial`,
    `B trial`, the version baked in when the release is built.
  - `trimixxx-splash.sh` picks the blob at start, from `rauc.slot=` on
    `/proc/cmdline` and the device tree. The choice is made on the deck, so
    both boot slots stay byte-identical (invariant 5).
  - One image serves every deck, so `make release` renders each set for every
    panel in `units/*.json` (trimixxx1's 1024×600, trimixxx2's 1280×800). The
    script takes the set whose geometry matches the framebuffer it finds, as
    it already checks today.
  - The dev card has no `rauc.slot=` and keeps today's plain logo.

### 4.2 Release tooling (`pi-qemu/release/`)

**`make release VERSION=v`** does the following, in order:

1. **Checks** that the tree is clean and the commit is tagged `v<VERSION>`.
2. **Builds the system** with `image/build.sh trimixxx-release`.
   - That's a card for a neutral hostname. It doesn't touch the shared golden
     snapshot, which only happens for `trimixxx0`.
   - Its base stage is cached after the first build.
3. **Takes out the root tree.** A privileged container loop-mounts the card's
   root partition read-only.
   - Check first that Docker Desktop allows it.
   - Fallback: boot a clone with `instance.sh`, stream
     `sudo tar --one-file-system --xattrs --acls -C / -cpf - .` over ssh, and
     read it with `mksquashfs - rootfs.squashfs -tar`.
4. **Seals it** with `exclude.txt`:
   - the hostname and `/etc/ssh/ssh_host_*`;
   - `/etc/machine-id`, emptied but not deleted (a mksquashfs pseudo-file);
   - the systemd random seed;
   - `/etc/NetworkManager/system-connections/*`: the build's `pi-qemu-home`
     and `trimixxx-hotspot`, both per-deck identity (§4.1);
   - `/etc/netplan/*`: cloud-init's and NetworkManager's leftovers, which
     eth0's keyfile replaces;
   - `/var/swap`, Pi OS's 2 GiB swap file;
   - `/var/lib/cloud`, logs, apt caches, shell histories, `/tmp`.
     `/etc/cloud/cloud-init.disabled` stays.
5. **Writes `/etc/trimixxx-release`** with the version.
6. **Builds the images with genimage:**
   - `rootfs.squashfs`: zstd, xattrs kept.
   - `boot.vfat`: the card's boot files, plus `config-release.txt` appended to
     `config.txt`, plus the `[0x<serial>]` screen sections built from
     `units/*.json`, plus `cmdline-a.txt` and `cmdline-b.txt` with the disk
     signature filled in. No `cmdline.txt`.
   - `bootsel.vfat` with `autoboot.txt` only. `make release` fails if it holds
     any `*.elf` (invariant 13).
   - Empty `data.ext4` and `state.ext4`, labelled `trimixxx-data` and
     `trimixxx-state`.
   - `trimixxx-<v>.img`: the layout in §1.4, about 13 GiB, for any card of
     16 GB or more.
     Slot B and the data partition are empty.
7. **Writes `manifest.txt`:** the commit, a sha256 for every output, and
   `dpkg -l` from the root.

**`make card DECK=name VERSION=v`** copies the release card and fills p7 from
`pi-qemu/.cache/decks/<name>/`:
- `trimixxx.conf`;
- `NetworkManager/*.nmconnection`: Wi-Fi, the hotspot (generated), and, for
  trimixxx0, `pi-qemu-home`;
- `ssh/ssh_host_*`.

Generate any missing host keys with `ssh-keygen -A -f`, and keep them: they
make `known_hosts` survive a reflash.

### 4.3 Tests

```sh
make -C pi-qemu/release card DECK=trimixxx0 VERSION=0.0.1   # the emulated deck's identity, pi-qemu-home included
pi-qemu/instance.sh up rel --from pi-qemu/release/out/0.0.1/trimixxx0-0.0.1.img
```

**Locked boot.** Checks:
- `findmnt /` shows overlay;
- a file created under `/etc` is gone after a reboot;
- `touch /data/x` fails, and `/var/lib/rauc` is writable;
- `/boot/firmware` is p2, read-only;
- `/proc/cmdline` has `rauc.slot=A`;
- the device tree's `partition` is 2;
- the hostname comes from `/data`;
- the splash shows `A` and the version (`instance.sh shot rel` during the
  first seconds);
- p1 holds only `autoboot.txt`;
- `/etc/netplan` is empty, eth0 is link-local through its keyfile, and the
  hotspot's profile comes from `/data`;
- `instance.sh console rel` answers;
- `instance.sh ready rel` passes.

**RAM measurement.**
- Mixxx needs tracks to analyse. Build a small FAT stick image of test tracks
  (`.cache/sticks/` is empty on MBP-NJ) and insert it.
- Load one track after another, and read the RAM layer's usage with
  `df /media/root-rw` (overlayroot's mount; check the name).
- Extrapolate to a 4-hour set (about 80 tracks), and record the result in
  `rauc-pi-4-setup.md` §3.3.

**Done when** a tag gives a card that passes these checks, with the RAM
measurement recorded.

**Done on 2026-10-08,** with rehearsal release 0.0.1 on trimixxx0's card:
- **Locked boot:** every check above passes.
  - `/` is overlayroot's overlay, a SquashFS p5 under a tmpfs.
  - A file written to `/etc` is gone after a reboot.
  - `/data` is read-only and `/var/lib/rauc` writable; `/boot/firmware` is
    p2, read-only.
  - `rauc.slot=A`, and the device tree's partition is 2.
  - The hostname, NetworkManager profiles, host keys and Mixxx files all come
    from `/data`.
  - The splash showed `A.raw`; p1 holds only `autoboot.txt`; the console
    answers; Mixxx is ready; no unit failed.
  - `rauc status` says booted from A, A activated.
- **RAM:** recorded in `rauc-pi-4-setup.md` §3.3. The layer grows about
  130 KiB per analysed track; the fork's own track cache, in `/tmp`, bounds
  itself.
- **Found and fixed:**
  - The deck listed overlayroot's `/media/root-ro` and `/media/root-rw` as
    sticks. The fork now counts a USB disk only.
  - genimage reads its inputs correctly through Docker's share of the Mac's
    disk, but `cp` doesn't: a file just written there can claim to be one
    hole, and the copy was zeros (a 6 KB bundle). The release now makes
    everything inside the container.
  - RAUC won't read a plain bundle off that share ("unsafe filesystem").
  - macOS's make (3.81) has no `.ONESHELL`, so the Mac-side recipes are
    single commands.
  - The backend printed its slot twice.

---

## 5. Phase 4: RAUC on the deck

### 5.1 Signing key

**Decided (Sam, 2026-10-08): the private key is public,** in the repo
(`pi-qemu/release/signing-key.pem`). TriMixxx is open hardware: anyone may sign
a release. RAUC still requires a signature, which then guards against a damaged
bundle, not a hostile one. Generated once, as planned:

```sh
openssl req -x509 -newkey rsa:4096 -nodes -days 36500 \
  -keyout key.pem -out cert.pem -subj "/CN=TriMixxx releases"
```

`cert.pem` is `pi_config/rauc/keyring.pem`, and `key.pem` is
`pi-qemu/release/signing-key.pem`.

### 5.2 Device files

They live in `pi_config/` and are deployed by the system step, inert on a dev
card; `make release` enables them.

- **`/etc/rauc/system.conf`**, exactly as `rauc-pi-4-setup.md` §3.5.
- **`/usr/lib/rauc/rpi-tryboot`**, adapted from
  `Rtone/raspberrypi-firmware-rauc-bootloader-backend` (LGPL-2.1). Keep its
  licence header and note where it came from. Its behaviour:
  - it implements the contract table in `rauc-pi-4-report.md` §5;
  - to write `autoboot.txt`, it mounts p1 at `/run/rauc/bootsel` and unmounts
    it afterwards;
  - it writes the file whole (invariant 2);
  - it arms the flag with `vcmailbox 0x00038064 4 0 1`;
  - it detects a trial from `/proc/device-tree/chosen/bootloader/{tryboot,partition}`;
  - it never runs `reboot '0 tryboot'`, which trixie's systemd refuses (T1);
  - stdout carries only the tokens RAUC expects; success is exit 0.

  **A copy of `autoboot.txt`.** The backend keeps a copy of every
  `autoboot.txt` it writes, in `/var/lib/rauc/autoboot.txt`. It writes and
  syncs the copy before p1's: ext4 has the journal FAT lacks, as Raspberry
  Pi's engineer advises (`rauc-pi-4-setup.md` App. A.4).

  **The `repair` subcommand** runs on normal (non-trial) starts. A damaged
  `autoboot.txt` always starts p2, even when B was committed (phase 1, T6c
  and T7). So:
  - if p1's file is missing or invalid, `repair` restores the copy;
  - if the copy names B, it then reboots into B;
  - if there's no copy, it writes one for the running slot;
  - if p1's file isn't the backend's own but commits the slot running, it
    was edited by hand, the fix for a deck that keeps restarting (F10). It's
    kept, and rewritten as the backend's own.
- **`trimixxx-health.service` and `trimixxx-health`:**
  - The unit runs at every start of a release card
    (`ConditionPathExists=/etc/rauc/system.conf`), and the script decides
    from the device tree's `tryboot`.
  - `After=NetworkManager.service ssh.service`. `TimeoutStartSec=180`.
  - **On a trial, one question: can the deck take the next release?**
    Decided by Sam on 2026-10-08, after phase 4: "As long as I can reflash,
    the update should stay there." It can when:
    - NetworkManager has it on a network: any of its connected states. That's
      `connected` on home Wi-Fi, and `connected (local only)` for the other
      two ways in, the Ethernet cable's link-local profile and the deck's own
      hotspot. On the loopback alone NetworkManager says `disconnected`. The
      first two were measured on the emulated deck; the hotspot, which
      pi-qemu can't run, is the same kind of connection (no route out). Not
      NetworkManager's "connectivity", which reads `none` on the cable;
    - and `ssh.service` is active.

    Nothing of the deck's own is checked: Mixxx, the MIDI bridge, the sound
    and the S3 may all be broken, and the next release fixes them over the
    air. It waits up to 150 s, counted on `/proc/uptime` (NTP jumps the wall
    clock at start). The hotspot starts 45 to 75 s after NetworkManager, when
    home Wi-Fi doesn't answer (`trimixxx-wifi-fallback`). Success: `rauc
    status mark-good`; if RAUC can't, the trial fails, since RAUC installs the
    next release. Failure: `rauc status mark-bad`, exit 1, then
    `FailureAction=reboot`.
  - **Every decision is also kept in `/var/lib/rauc/trimixxx-health.log`,**
    on the state partition both slots share, with one previous file at
    64 KiB. The journal is in RAM, so after a trial failed this file is what
    says why the update rolled back.
  - **On a normal start:** `rpi-tryboot repair`, and the reboot it may ask
    for; then the reconcile below.
  - What resets a trial that never gets this far:
    - until systemd starts, the kernel watchdog (phase 1, T8; F6b);
    - after that, systemd itself, which holds `/dev/watchdog0`
      (`RuntimeWatchdogSec=1min` on the release, as Pi OS sets it), so a hung
      PID 1 resets too;
    - a trial stuck anywhere else ends at this unit's timeout.
- **Reconcile after a boot-watchdog fallback.** The decks set the EEPROM's
  boot watchdog (invariant 14). When the committed slot fails before Linux
  starts, the bootloader starts p2 after 45 s (phase 1, T10).
  - **How it shows:** a normal start (`tryboot` 0) of a slot other than the
    one a valid `autoboot.txt` commits. A deliberate `reboot N` looks the
    same.
  - **What runs:** the health unit, as for a trial.
  - **If the running slot passes:** `rauc status mark-bad other`, then commit
    the running slot.
  - **If it fails:** nothing changes, and a hand is needed.
  - **What it covers:** A is the fallback, as for a damaged `autoboot.txt`,
    so this rescues decks whose committed slot is B.
  - **What still loops** until a hand fixes it: a committed A that fails
    before Linux, and any committed slot that hangs after Linux starts (T9,
    F10).

### 5.3 Bundles and shipping

- **`make release` also builds `trimixxx-<v>.raucb`.**
  - The manifest comes from `manifest.raucm.in`: `compatible=trimixxx-pi4`,
    the version, `build=` from `git describe`, `format=plain`,
    `[image.rootfs] filename=rootfs.squashfs` and
    `[image.boot] filename=boot.vfat`.
  - It's signed with `rauc bundle --cert --key` in the container.
  - Stage the input directory inside the container, with plain files only
    (RAUC 1.13).
- **`make ship DECK=name VERSION=v`** runs:
  1. the bundle into `name:/var/lib/rauc/`, streamed through `ssh … sudo sh
     -c 'cat > …'`: only root writes there, and `/tmp` is RAM;
  2. `ssh name sudo rauc install …`, then deletes the bundle;
  3. a reboot;
  4. waits for a new boot ID over ssh;
  5. `ssh name rauc status`, with the release and slot the deck runs.

  For emulated decks, run it inside `pi-qemu/instance.sh run NAME -- …`.
- **`make faults VERSION=v`** makes the broken releases of the fault table
  from release *v*'s images, each signed like it: `v-nonet` (F4a), `v-nossh`
  (F4b), `v-noinitramfs` (F5), `v-panic` (F6a) and `v-hang` (F6b), in `out/`,
  for `make ship`.

### 5.4 The fault matrix in QEMU

Run it on an A/B deck started from release *v1*, shipping *v2*:

| # | Fault | How | Expected |
|---|---|---|---|
| F1 | Damaged bundle | install a truncated copy | refused; nothing written; `rauc status` unchanged |
| F2 | Power lost during install | power off without a sync from the deck (`echo o > /proc/sysrq-trigger`), 1 s after `rauc install` starts copying the root image | A; B not marked good |
| F3 | Power lost after install, before the reboot | `instance.sh kill`, then `up` | A |
| F4a | The trial can't be reached: no network | `make faults`' `-nonet`: NetworkManager masked | trial → mark-bad after 150 s → reboot → A |
| F4b | The same: no sshd | `-nossh`: `ssh.service` masked | trial → mark-bad after 150 s → reboot → A |
| F5 | Panic on the trial | `-noinitramfs`: no initramfs to mount the SquashFS root | `panic=10` → A |
| F6a | The trial gives up in its initramfs | `-panic`: `break=premount` | initramfs-tools' `panic=10` → A |
| F6b | Hang on the trial | `-hang`: `break=premount` without `panic=10` | watchdog → A, about 2 min (118 s on trimixxx3, T8) |
| F7a | `autoboot.txt` broken (power lost during a commit), A committed | corrupt it on a running deck, then reboot | p2 starts (T6c, T7); `repair` restores the file |
| F7b | The same, B committed | after F9 | p2 (A) starts; `repair` restores the copy, then reboots into B |
| F8 | Rollback | `rauc status mark-active other`, then reboot | trial of the old version → commit |
| F9 | Normal update | ship v2 | B committed; A keeps v1. The splash shows `B trial` and v2 during the trial, then `B` and v2 |
| F10 | The committed slot hangs | as F6b, on the committed slot's command line | it keeps restarting: the known gap (T9). Checks that pi-qemu copies the hardware. The fix, on the Mac: commit the other slot in `autoboot.txt` (the volume `BOOTSEL`) |
| F11 | Boot-watchdog fallback, B committed | `systemctl reboot --reboot-argument=2`. pi-qemu runs no firmware that could hang, so this leaves the deck as the watchdog would | A passes the health check; B marked bad; A committed |

**Done when** F1 to F11 behave as listed in QEMU.

**Done on 2026-10-08,** on the emulated deck `ab`: rehearsal release 0.0.1's
card for trimixxx0, updated over the air through 0.0.2, 0.0.4 and 0.0.6, with
broken releases made from them. Every row behaves as listed:

| # | Result |
|---|---|
| F1 | A bundle cut at 100 MB: `rauc install` fails; p6 and `rauc status` unchanged |
| F2 | Off 1 s into the copy of 0.0.2 over 0.0.4: A starts. B is marked bad, RAUC records its write as `pending`, and p6 matches neither release. The copy lasts about 3 s in the emulator, too short to time a power-off from the Mac |
| F3 | After the install the flag read 1 (`vcmailbox 0x00030064`); the power cut forgot it. A starts; B keeps a complete 0.0.2, marked bad |
| F4a | Trial of `0.0.8-nonet`: unreachable, as it should be; mark-bad after 150 s, reboot: the committed slot, 177 s after the reboot command. Its log on the state partition: `can't take the next release after 150 s (no network: NetworkManager says nothing)` |
| F4b | `0.0.7-nossh`: the same, back on the committed slot after 178 s |
| F5 | The kernel panics without a root, `panic=10` reboots: A after 94 s (0.0.5, before `rootdelay` went) |
| F6a | initramfs-tools reboots 10 s after giving up: A after 28 s |
| F6b | The shell waits, the watchdog resets 61 s into the hang: A after 77 s. Phase 2 measured 75 s for T8 in pi-qemu, against 118 s on trimixxx3, which includes two real firmware starts |
| F7a | p1's `autoboot.txt` deleted, A committed: A starts in 15 s; `repair` restores the file; no reboot |
| F7b | The file cut short, B committed: p2 (A) starts in 15 s; `repair` restores the copy and reboots; B 15 s later. Also passed before `repair` changed (68 s, with `rootdelay`) |
| F8 | `mark-active other`: trial of B with the older 0.0.2, committed 8 s after start |
| F9 | 0.0.2, 0.0.4, 0.0.6, 0.0.7 and 0.0.8 shipped into each slot in turn: trial, then committed; the other slot keeps the previous release. `make ship` takes 41 s with the 1.1 GB bundle. The splash showed `B-trial.raw` ("B · trial · v0.0.6") during B's trial, then `B.raw`. With the health check Sam chose after the matrix, a trial is committed 6 s after the kernel starts, before Mixxx is up |
| F10 | B committed and made to hang: B restarts every 61 s, as T9 did on trimixxx3. Fixed on the Mac by committing A in `autoboot.txt` as an editor might leave it (CRLF, no `[tryboot]`): A starts, and `repair` keeps the edit, rewritten as the backend's own |
| F11 | `reboot 2` with B committed: A starts, passes, and is committed; B marked bad |

Not covered in pi-qemu, as planned: the EEPROM's boot watchdog, measured on
trimixxx3 in T10. F11 stands in for it.

**Changed after the matrix (Sam, 2026-10-08): the health check asks only
whether the deck can take the next release** (a network by any of the three
ways in, and sshd; §5.2). It checked Mixxx's sound stream, the S3's
controller and the version before, so a deck without its sound card failed
every trial, and a release whose only fault was there rolled back instead of
waiting for the next one. F4 became F4a and F4b, and `-nomapping` left
`make faults`. F4 to F9 ran again on 0.0.7 and 0.0.8.

**Found and fixed:**
- **`rootdelay=20` cost every start 20 s.** initramfs-tools sleeps that long
  before it even looks for the root. It already waits up to 30 s for a missing
  root on its own. The release's command lines no longer carry it: kernel and
  initramfs take 1.8 s, down from 21.9 s.
- **With `panic=` set, initramfs-tools never opens a shell.** `break=premount`
  reboots after 10 s instead of hanging. F6 was such a reboot, so it is now
  two rows, and the hang row (F6b) drops `panic=10`.
- **`repair` would have undone the fix for F10.** It counted only its own bytes
  as valid, so an `autoboot.txt` edited by hand was "restored" from the copy,
  and the deck went back into its loop. A file that commits the slot running
  now wins.
- **The health check timed its 150 s on the wall clock,** which NTP jumps
  forward at start. Its first F4 trial ended the moment NTP answered. It
  counts `/proc/uptime` now.
- **A failed trial's reason was lost** at the reboot that ends it: the
  journal is in RAM, and Debian's console level (`kernel.printk = 4`) keeps
  the health check's kernel-log lines off the console. Its decisions now also
  go to `/var/lib/rauc/trimixxx-health.log`.
- **pi-qemu stopped at a missing initramfs.** The firmware starts the kernel
  without one (F5), and pi-qemu now does too, with a note.
- **`make ship` copied the bundle through `/tmp`,** which is RAM (tmpfs, as
  on any trixie): 1.1 GB of it, on a deck with 4 GB. It streams the bundle
  straight into `/var/lib/rauc` through `sudo` now, and deletes it after the
  install.

---

## 6. Phase 5: trimixxx3 end to end

1. Make the per-deck card with `make card DECK=trimixxx3`.
   - Its bootloader is already 2026-05-17 (phase 1, T0b).
   - Secrets go in `pi-qemu/.cache/decks/trimixxx3/`.
   - The bench works over the direct Ethernet cable (eth0 is link-local on
     the decks anyway), so Wi-Fi is optional.
2. Sam flashes the card.
3. The checks of §4.3 on the hardware.
4. Ship two releases with `make ship DECK=trimixxx3`.
5. F1 to F9 on the hardware, F6 both ways. `make faults VERSION=v` makes the
   broken releases that F4 to F6 ship. Sam pulls the power where the matrix
   says so: for F2, while `rauc install` says `Copying image to rootfs`, which
   lasts far longer on a real SD card than the emulator's 3 s.

The bench needs neither a sound card nor an S3: the health check only asks
that the deck can take the next release, and the Ethernet cable gives it
that. Its HDMI screen shows the splash's slot label (F9).

**Done when** F1 to F9 pass on trimixxx3. F10 was phase 1's T9.

---

## 7. Phase 6: the decks (only with Sam)

For each deck, one at a time, on a spare card first:
1. **Capture what lives only on the deck:**
   - trimixxx1's `config.txt` and panel;
   - the MAC addresses;
   - `apt-mark showmanual`;
   - `asound.state`;
   - the EEPROM's config and version (`rpi-eeprom-config`,
     `vcgencmd bootloader_version`);
   - its NetworkManager profiles (`/etc/netplan/90-NM-*.yaml`,
     `/etc/NetworkManager/system-connections/`): its Wi-Fi goes into the
     identity;
   - any Mixxx state worth keeping.
2. **Update the bootloader, and check it** (invariant 14).
   - On the deck's current OS, `/boot/firmware` is p1, so the stock
     `sudo rpi-eeprom-update -a` and a reboot work.
   - `vcgencmd bootloader_version` must then show 2026-05-17 or later.
   - Set the boot watchdog with `rpi-eeprom-config --apply`:
     `BOOT_WATCHDOG_TIMEOUT=45`, `BOOT_WATCHDOG_PARTITION=2`. First check in
     `sudo vclog --msg` that the deck reaches `Starting ARM` well within
     45 s (trimixxx3: 15.7 s).
   - Once the A/B card is in, an EEPROM update is staged on p1 with `BOOTFS=`
     (`rauc-pi-4-setup.md` App. A.4).
3. **Record its serial** in `units/<deck>.json`.
   - **Not built yet:** `make release` doesn't write the `[0x<serial>]` screen
     sections from `units/*.json` (§4.2, step 6). Today's release
     `config.txt` loads no panel overlay, so a deck's own panel (trimixxx2's
     DSI one) would stay dark. A bare Pi on HDMI, like trimixxx3, doesn't
     need them. Build them before the first deck's card.
4. Run `make card`. **Sam flashes** the card.
5. Verify the deck, then ship one release over the air.

**Done when** every deck has taken a release over the air.

---

# Part 2. pi-qemu foundations (built)

Why pi-qemu is built the way it is. The sections of the old plan on the card
layout and network updates (D7, D8, §7, §9) are replaced by Part 1.

| # | Decision |
|---|---|
| D1 | **The real board.** QEMU's `raspi4b` (BCM2711), with two sets of patches. [rpi-qemu](https://github.com/fpgas-online/rpi-qemu)'s 41 cover GENET, the PCIe root complex, the firmware's device-tree fix-ups and the watchdog trixie needs to boot. Ours (`qemu/trimixxx-patches.py`) cover HVF, PCIe with xHCI behind it, SD card DMA, the full RAM, the deck's sound card at 44.1 kHz, saved state for every device, and a fast SD card. Not QEMU's generic `virt` machine, which can't run the Pi's kernel |
| D2 | **One image, byte-identical.** pi-qemu does the VideoCore firmware's job on the host and hands QEMU the kernel, DTB and initramfs off the card. That job covers `autoboot.txt`, `config.txt` with its filters and overlays (through Raspberry Pi's own `dtmerge`), `cmdline.txt`, and the device-tree edits. Graphics overlays are skipped; the kernel uses the firmware framebuffer at the panel's size |
| D3 | **The S3 link** is the Pi's own PL011 (`ttyAMA0`, made `serial0` by `dtoverlay=disable-bt`), backed by a Unix socket. The panel or the CLI is the S3 |
| D4 | **Per-deck parameters:** hostname, accent, MAC. The hostname picks `mixxx_config/units/<host>.json` |
| D5 | **MAC:** a Pi uses its own; an emulated deck gets `02:54:4d:58:00:NN`, written into the device tree where the firmware writes the real one |
| D6 | **trimixxx0** is a virtual trimixxx2: its wiring and 1280×800 panel |
| D9 | **The panel** is a desktop window in Qt (the plan said a web page) |

**Measured** in 2026-10, on an M1:
- the stock trixie card boots to login in about 5 s (from about 50 s, before
  the SD fixes);
- a golden snapshot restores in 3–4 s;
- the CPU runs at M1-native speed through HVF;
- the board has 1.8 GiB of its 2 GiB;
- the SD card reads at about 800 MB/s and writes at about 400 MB/s;
- USB audio appears as card 0, and sticks as `sda` at 5 Gb/s.

**Done in QEMU:**
- the UCA222 sound card;
- saved state for every device (snapshots);
- the fast SD card.

**Still open:**
- **Q1**, a 4 GiB board (the decks have 4 GB);
- the reboot flags and `tryboot` (phase 2 above);
- offering the PCIe, SD, RAM and HVF fixes to rpi-qemu.

**Risks:**
- **GPU:** llvmpipe, not V3D. The emulated deck tests behaviour, not the GPU
  or timing.
- **CPU:** the M1 is newer than the A72. TCG (`--accel tcg`) emulates a real
  Cortex-A72 to catch illegal instructions, slowly.
- **Out-of-tree QEMU:** both patch sets are pinned, and the build fails loudly
  if the code moves.
- **Little Snitch**, or any outbound filter, can hold a new QEMU binary's
  first packet and freeze the guest. Allow `qemu-system-aarch64`.
