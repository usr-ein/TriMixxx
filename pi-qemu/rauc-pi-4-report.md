# TriMixxx A/B updates: the report, for builders

The Markdown form of the visual report (`rauc-pi-4-report.html`). It's written
for an agent that builds the system: every diagram is here as text (Mermaid),
with the rendered figure beside it for people.

| File | Role |
|---|---|
| `PLAN.md` | **What to do, in order**: phases, tasks, tests, stop points. Start there |
| this file | **How the system works**: the report's ten sections, in text |
| `rauc-pi-4-setup.md` | **Design and evidence**: every configuration file, the research, the sources |
| `rauc-pi-4-report.html`, `rauc-pi-4-figures/` | The visual report and its figures |

Built and tested through phase 5 (2026-10-08): the card, the updates, and the
fault table, on the emulated deck and on trimixxx3, the bench Pi. The decks
come next.
Where this file and `PLAN.md` differ, `PLAN.md` wins.

---

## Vocabulary

| Term | Meaning |
|---|---|
| **Slot A / slot B** | One complete copy of the system: a FAT boot partition plus a SquashFS system partition. A = p2 + p5, B = p3 + p6 |
| **bootsel** | p1, a small FAT partition holding only `autoboot.txt`, the file that picks the slot. No firmware may live there: then a damaged `autoboot.txt` still starts A (measured) |
| **Committed slot** | The slot named by `[all] boot_partition` in `autoboot.txt`; it starts by default |
| **Trial start** | One start of the other slot, made by the firmware's one-shot **tryboot** flag |
| **Commit** | Rewriting `autoboot.txt` so the slot that passed its trial becomes the default |
| **RAUC** | The A/B updater (Debian `rauc` + `rauc-service`, 1.13). Installs bundles into the idle slot |
| **Bundle** | `trimixxx-<v>.raucb`: a signed file holding `boot.vfat` and `rootfs.squashfs` |
| **rpi-tryboot** | Our RAUC custom bootloader backend (~100 lines of shell, adapted from Rtone's) |
| **Health check** | Our systemd unit that, on a trial start, runs `rauc status mark-good` if the deck can take the next release (it's on a network and sshd runs), or `mark-bad` |
| **Identity unit** | Our systemd unit that applies the deck's name, wiring and accent from `/data` |
| **Dev deck** | The writable emulated deck (`pi-qemu image build`'s card) used for feature work, as today |
| **A/B deck** | An emulated deck booted from a release card image, used for update work |
| **trimixxx3** | The spare Pi 4, used as the bench deck |

---

## 1. The card

![Figure 1: the card](rauc-pi-4-figures/fig1-card.png)

```text
MBR, fixed disk signature 0x5d0bc1ec (<id> = 5d0bc1ec)
| p1 bootsel | p2 boot-A | p3 boot-B | p4 extended ───────────────────────────────────────────── |
| FAT32      | FAT32     | FAT32     | p5 rootfs-A  | p6 rootfs-B  | p7 data     | p8 state       |
| 64 MiB     | 512 MiB   | 512 MiB   | SquashFS 4G  | SquashFS 4G  | ext4 64 MiB | ext4, the rest |
```

| # | Holds | Mounted (slot A running) | Written |
|---|---|---|---|
| p1 bootsel | `autoboot.txt` | no | only at commit, by the backend |
| p2 boot-A | firmware, kernel, initramfs, DTBs, overlays, `config.txt`, `cmdline-a.txt`, `cmdline-b.txt` | `/boot/firmware`, read-only | **never** while A runs |
| p3 boot-B | the same files | no | by RAUC (idle slot) |
| p5 rootfs-A | the system, SquashFS | `/`, under the RAM layer | **never** while A runs |
| p6 rootfs-B | the next system | no | by RAUC (idle slot) |
| p7 data | deck name, Wi-Fi keyfiles, ssh host keys | `/data`, read-only | only when unlocked by hand |
| p8 state | RAUC's data directory, bundles | `/var/lib/rauc`, read-write | by RAUC |

- The image ends at about 13 GiB, so it fits any card of 16 GB or more; the
  rest of a bigger card stays unused.
- Only the **emulator's** card file must be a power of two; it is rounded up
  as a sparse file.

---

## 2. How a deck starts

![Figure 2: how a deck starts](rauc-pi-4-figures/fig2-boot.png)

```mermaid
flowchart TD
  FLAG["Trial flag, one start only<br/>set by: systemctl reboot --reboot-argument='0 tryboot', or vcmailbox 0x00038064 4 0 1<br/>cleared when used, lost at power-off"]
  BL["EEPROM bootloader (chip on the board)"]
  P1["p1 bootsel: autoboot.txt<br/>[all] tryboot_a_b=1, boot_partition=2<br/>[tryboot] boot_partition=3"]
  P2["p2 boot-A: start4.elf reads config.txt<br/>[boot_partition=2] cmdline=cmdline-a.txt"]
  P3["p3 boot-B: the same files and config.txt<br/>[boot_partition=3] cmdline=cmdline-b.txt"]
  P5["p5 rootfs-A: SquashFS"]
  P6["p6 rootfs-B: SquashFS"]
  LX["Linux: initramfs mounts the SquashFS read-only and overlayroot adds a tmpfs on top<br/>systemd starts Mixxx. On a trial start, the health check decides"]
  FLAG -.->|read once| BL
  BL -->|reads| P1
  P1 -->|default| P2
  P1 -.->|trial flag set| P3
  P2 -->|"root=PARTUUID=…-05 rauc.slot=A"| P5
  P3 -.->|"root=PARTUUID=…-06 rauc.slot=B"| P6
  P5 --> LX
  P6 -.-> LX
```

**Facts this rests on.** All are Raspberry Pi's documented mechanism; see
`rauc-pi-4-setup.md` App. A.
- `autoboot.txt` on the first FAT partition picks the boot partition. Its
  `[tryboot]` section applies when the flag is set. `tryboot_a_b=1` makes the
  trial partition read its normal `config.txt`.
- `[boot_partition=N]` in `config.txt` picks a different `cmdline` per boot
  partition, so **both boot partitions hold identical files**.
- The OS reads what the firmware did under `/proc/device-tree/chosen/bootloader/`.
  The values are 32-bit big-endian: `tryboot` (1 on a trial), `partition`
  (the boot partition used), and more. Phase 1 records the exact names on a
  real Pi.

`cmdline-a.txt` (B: `-06`, `rauc.slot=B`, `-03`):

```text
console=tty1 root=PARTUUID=<id>-05 rootfstype=squashfs rootwait=20 panic=10 overlayroot=tmpfs:recurse=0 rauc.slot=A systemd.mount-extra=PARTUUID=<id>-02:/boot/firmware:vfat:ro <the deck's current flags>
```

Never put `resize`, or an `init=` first-boot hook, on these lines. Pi OS's
first boot rewrites the disk ID, which would break `PARTUUID=`.

---

## 3. The running system

![Figure 3: the running system](rauc-pi-4-figures/fig3-running-system.png)

```mermaid
flowchart LR
  subgraph ROOT["/ as every program sees it"]
    RAM["RAM layer: tmpfs from overlayroot<br/>every write lands here<br/>at most half the RAM, gone at restart"]
    SQ["System slot: SquashFS<br/>read-only, even for root"]
  end
  MW["Mixxx saves its library"] -->|writes| RAM
  MR["Mixxx loads its skin"] -->|reads, falls through| SQ
  BF["/boot/firmware<br/>the running boot slot, read-only<br/>(systemd.mount-extra)"]
  ST["/var/lib/rauc<br/>p8, read-write, RAUC only"]
  DA["/data<br/>p7, read-only identity"]
  ID["identity unit, sshd, NetworkManager"] -->|reads| DA
  RA["RAUC"] -->|records, bundles| ST
```

- **overlayroot** is Debian's package (`overlayroot=tmpfs` on the kernel line),
  the same one `raspi-config` uses. The initramfs needs `squashfs` and
  `overlay` listed in `/etc/initramfs-tools/modules`.
- **The RAM layer is bounded.** The kernel caps a size-less tmpfs at half the
  RAM; beyond that writes fail, nothing crashes.
- **Writers are capped:**
  - Mixxx logs: `/tmp/mixxx`, 2 × 32 MiB;
  - journald: volatile, 10 % of `/run`;
  - core dumps: off, via a `coredump.conf` drop-in, `Storage=none`.

  Mixxx's library growth over a set is measured in phase 3.
- **Identity:** the identity unit reads `/data` early and writes only into
  RAM, so NetworkManager and sshd aren't reconfigured, and the dev card (no
  `/data`) is unaffected.
  - Wi-Fi: `/data/NetworkManager/*.nmconnection`, copied to
    `/run/NetworkManager/system-connections/`.
  - ssh host keys: `/data/ssh/ssh_host_*`, copied to `/etc/ssh/`.
  - Deck name: `/data/trimixxx.conf`. The unit sets the hostname and links the
    deck's pre-rendered Mixxx files.
- **machine-id** is regenerated at each start, systemd's standard behaviour on
  a read-only root. NetworkManager uses `dhcp-client-id=mac` so leases stay
  stable.

---

## 4. One update, as states

![Figure 4: update states](rauc-pi-4-figures/fig4-update-states.png)

```mermaid
stateDiagram-v2
  RunningA: Running A (A is the default)
  InstalledB: B installed (flag armed for one start)
  TrialB: Trial start of B (health check running)
  RunningB: Running B (B is the default, A kept)
  [*] --> RunningA
  RunningA --> InstalledB: rauc install writes p3 + p6, backend arms the flag
  InstalledB --> TrialB: reboot (any reboot)
  InstalledB --> RunningA: power-off before reboot (flag lost)
  TrialB --> RunningB: check passes, rauc status mark-good commits
  TrialB --> RunningA: check fails, hang, panic or power cut (flag already gone)
```

**Rollback** is `rauc status mark-active other`, then a reboot: a trial of
the other slot, like any update.

---

## 5. Who does what during an update

![Figure 5: update sequence](rauc-pi-4-figures/fig5-update-sequence.png)

```mermaid
sequenceDiagram
  autonumber
  actor You as You (Mac, ssh)
  participant RAUC as RAUC (deck)
  participant BE as rpi-tryboot (our backend)
  participant AB as autoboot.txt (p1)
  participant FW as Pi firmware
  participant HC as Health check (our unit)
  You->>RAUC: rauc install /var/lib/rauc/trimixxx-1.0.1.raucb
  RAUC->>BE: set-state B bad
  RAUC->>RAUC: check the signature, write boot.vfat to p3 and rootfs.squashfs to p6
  RAUC->>BE: set-primary B
  BE->>AB: write the whole file with [tryboot] boot_partition=3, fsync, rename
  BE->>FW: vcmailbox 0x00038064 4 0 1 (arm the flag)
  You->>FW: sudo reboot
  FW->>AB: read the [tryboot] section
  FW->>FW: start p3 once, clear the flag
  HC->>HC: on a network (cable, home Wi-Fi or hotspot), sshd running
  HC->>RAUC: rauc status mark-good
  RAUC->>BE: set-state B good
  BE->>AB: write the whole file with [all] boot_partition=3 (the commit)
```

**The backend contract.** RAUC 1.13 calls the backend as follows;
`rauc-pi-4-setup.md` App. B.5 has the full table.

| RAUC calls | When | Backend behaviour |
|---|---|---|
| `get-primary` | `rauc status` | print the bootname of `[all] boot_partition` (2 → A, 3 → B), from the backend's copy in `/var/lib/rauc` if p1's file is damaged |
| `set-state X bad` | start of an install; `mark-bad` | nothing to undo: `[all]` still names the old slot. Exit 0 |
| `set-primary X` | end of an install; `mark-active` | write `autoboot.txt` with `[tryboot] boot_partition=` X's boot partition, then arm the flag |
| `set-state X good` | `mark-good` | if X is running on trial (device tree `tryboot` = 1, `partition` = X's), commit: write the copy in `/var/lib/rauc`, then `autoboot.txt`. Otherwise do nothing |
| `get-state X` | `rauc status` | `good` for the running slot, `bad` for the other |
| `get-current` | never in 1.13, because `rauc.slot=` wins | bootname from `/chosen/bootloader/partition` |

---

## 6. The emulated deck and the real one

![Figure 6: emulated vs real](rauc-pi-4-figures/fig6-emulated-vs-real.png)

```mermaid
flowchart TB
  subgraph REAL["Real deck: trimixxx1, 2, 3"]
    direction TB
    R1["SD card: p1 to p8, any size"] --> R2["EEPROM bootloader<br/>reads p1 autoboot.txt, holds the trial flag"]
    R2 --> R3["start4.elf<br/>config.txt filters, overlays, device tree,<br/>cmdline-a or -b, writes /chosen/bootloader"]
    R3 --> R4["Raspberry Pi 4: Cortex-A72, 4 GB, real watchdog"]
  end
  subgraph EMU["Emulated deck: pi-qemu on the Mac"]
    direction TB
    E1["Card file: the same p1 to p8<br/>sparse, rounded up to a power of two"] --> E2["pi-qemu firmware step (our C++)<br/>reads the same autoboot.txt<br/>trial flag from QEMU's mailbox over QMP"]
    E2 --> E3["pi-qemu, from the same files<br/>the same config.txt rules, overlays via dtmerge<br/>writes /chosen/bootloader"]
    E3 --> E4["QEMU raspi4b: the Mac's cores through HVF, 2 GB, watchdog model"]
  end
  R4 --> SW["The same software: kernel, initramfs, SquashFS system,<br/>RAUC, rpi-tryboot, health check, Mixxx"]
  E4 --> SW
```

| | Real deck | Emulated deck | |
|---|---|---|---|
| Card | SD card, any size | a sparse file, a power of two | same layout |
| Who chooses the slot | EEPROM bootloader | pi-qemu's firmware step | same rules |
| Trial flag | firmware memory, lost at power-off | QEMU mailbox (patched), read by pi-qemu at reboot | same behaviour |
| Kernel, initramfs, system, RAUC, backend, health check | the same files | the same files | identical |
| CPU and RAM | Cortex-A72, 4 GB | Mac cores, 2 GB | differs |
| Screen | DSI panel, GPU | framebuffer, llvmpipe | differs |
| Network | Wi-Fi at home; eth0 for CDJs | USB NIC for ssh; eth0 | differs |
| Pulling the plug | real power loss | `pi-qemu deck power NAME off` | differs |

**Only real hardware (trimixxx3) can show** what the real `start4.elf` and
EEPROM do: `[boot_partition=N]` on a Pi 4, a broken `autoboot.txt`, the
watchdog's timing, and an SD card losing power. **Phase 1 measures these
first; pi-qemu then copies what was measured.**

---

## 7. How you work with it

![Figure 7: workflow](rauc-pi-4-figures/fig7-workflow.png)

```mermaid
flowchart LR
  REPO["Repo: commits"] --> BUILD["pi-qemu image build<br/>system built in QEMU"]
  BUILD --> DEV["Dev deck (emulated)<br/>writable, 2 partitions"]
  DEV -->|"pi-qemu deck deploy: change, test, repeat"| DEV
  BUILD -->|tagged commits only| REL["pi-qemu release build<br/>seal, lock, genimage, rauc bundle"]
  REL --> BUNDLE["trimixxx-v.raucb (signed)"]
  REL --> IMG["trimixxx-v.img (8 partitions)"]
  BUNDLE --> AB["1. Emulated A/B deck: rehearse, fault matrix"]
  AB --> BENCH["2. trimixxx3 bench Pi"]
  BENCH --> DECKS["3. Decks: pi-qemu deck ship"]
  IMG -->|"pi-qemu release card NAME adds /data"| SD["SD card, flashed once per deck"]
  IMG -.->|previous release| AB
```

```sh
# day to day (unchanged)
pi-qemu deck up dev
pi-qemu deck deploy dev mixxx                 # or config, system, launcher...

# a release
git tag -m "TriMixxx 1.0.1" pi/v1.0.1
pi-qemu release build                         # the tag's version: out/1.0.1/: trimixxx-1.0.1.img + .raucb + manifest

# rehearse on the emulated A/B deck (started from the previous release's card)
pi-qemu release card trimixxx0 --version 1.0.0   # the emulated deck's card of 1.0.0
pi-qemu deck up ab --from out/1.0.0/trimixxx0-1.0.0.img
pi-qemu deck ship ab --version 1.0.1
pi-qemu deck ssh ab rauc status

# ship to a deck, roll back
pi-qemu deck ship --host trimixxx3 --version 1.0.1   # the bundle over ssh, rauc install, reboot, rauc status
ssh trimixxx3 sudo rauc status mark-active other && ssh trimixxx3 sudo reboot

# a new deck: one physical flash
pi-qemu release card trimixxx4                # the release card with that deck's /data
```

---

## 8. When something goes wrong

Every row is rehearsed on the emulated deck first, then on trimixxx3. All of
them passed on the emulated deck, and on trimixxx3 where only hardware can
tell, power cuts and the watchdog included, on 2026-10-08 (`PLAN.md` §5.4,
§6).

| Fault | What catches it | Ends on |
|---|---|---|
| Damaged bundle, or one you didn't sign | RAUC's signature check, before writing | A |
| Power lost during install | only the idle slot was being written | A |
| Power lost after install, before the reboot | the flag doesn't survive power-off | A (install again) |
| New system doesn't boot | `panic=10`, in the kernel and in the initramfs; the flag is gone | A |
| New system freezes | hardware watchdog armed from the firmware (`kernel_watchdog_timeout`): about 60 s into the hang | A |
| It boots, but can't take the next release: no network, or no sshd | health check: `mark-bad`, then a reboot | A |
| It can take the next release, but Mixxx, the sound or the S3 fails | nothing, on purpose: the next release fixes it over the air | the new slot |
| Power lost during the commit | a copy written first on the state partition, then `autoboot.txt` whole and synced. A broken `autoboot.txt` starts p2 (measured); the start-up `repair` restores the copy, and reboots into B if B was committed | the committed slot |
| A committed slot breaks later | tryboot's one weak spot. Measured: `kernel_watchdog_partition` doesn't switch slots, so a hang in Linux loops. The decks set the EEPROM's boot watchdog: a failure before Linux then starts A after 45 s, and the backend commits A if it passes the health check. Otherwise, on a Mac, change the slot `autoboot.txt` commits (the volume `BOOTSEL`): the deck keeps that edit | A when B was committed; otherwise a hand on a Mac |

---

## 9. Standard parts and our own

| Job | Part | Kind | Why we write any of it |
|---|---|---|---|
| Choose the slot, trial, fall back | Pi firmware `autoboot.txt` + tryboot | standard | — |
| Command line per slot | `config.txt` `[boot_partition=N]` | standard | Saves an install hook |
| Check, install and record updates | RAUC 1.13 (Debian) | standard | — |
| Read-only system, RAM layer | SquashFS + `overlayroot` | standard | — |
| Restarts on hangs and panics | firmware/systemd watchdogs, `panic=` | settings | — |
| Wi-Fi, ssh keys, `/data` lock | standard NetworkManager keyfiles and OpenSSH host keys, an fstab `ro` mount | formats and settings | — |
| Build the card and bundle | genimage, mksquashfs, `rauc bundle` in Docker | config files | `genimage.cfg`, Dockerfile, manifest, Make targets |
| Connect RAUC to the Pi firmware | `rpi-tryboot` (~100 lines) | **ours** | RAUC 1.13 has no Pi firmware backend. Delete it when RAUC's own (PR #1599, aimed at 1.17) ships |
| Decide the deck is healthy | systemd unit calling `rauc status mark-good` | **ours** (the checks) | What "healthy" means is deck-specific |
| Who this deck is | identity unit | **ours** | Per-device app config has no standard tool |
| A/B in the emulator | pi-qemu firmware step + one QEMU patch | **ours** | QEMU runs no Pi firmware |

**Why tryboot and not U-Boot.** U-Boot would leave the firmware, device tree
and `config.txt` outside A/B. It would write the card at every boot, has no
Pi watchdog support, and adds boot time. Full comparison:
`rauc-pi-4-setup.md` App. C.2.

---

## 10. Phases

The detail (tasks, commands, tests, stop points) is in `PLAN.md`.

| # | Phase | Done when |
|---|---|---|
| 1 | Firmware facts on trimixxx3 | every unverified firmware point has a measured answer |
| 2 | Tryboot in pi-qemu | the phase 1 card behaves the same in pi-qemu as on trimixxx3 |
| 3 | A locked card | a tag gives a card that boots locked in QEMU, its splash showing the slot (`A`/`B`, `trial`) and the version; the RAM layer is measured over a set |
| 4 | RAUC on the deck | the whole fault table passes in QEMU |
| 5 | trimixxx3 end to end | the fault table passes on hardware |
| 6 | The decks (only with Sam) | every deck has taken a release over the air |
