# pi-qemu — trimixxx0, an emulated deck, and one image for every deck

**trimixxx0** is an emulated Raspberry Pi 4 — QEMU's `raspi4b` board, patched,
running under the M1's hardware virtualisation — that boots the deck's own SD
card image unmodified: the same Raspberry Pi kernel, device tree, initramfs and
root filesystem. Its screen shows at the panel's resolution; it is played from
a to-scale drawing of trimixxx2's top plate that speaks the S3's MIDI on the
Pi's real UART. The card image it boots is the one the decks are flashed with
once, and after that they update over the network into A/B slots.

Status (2026-10-04): **prototype proven, nothing in the repo yet but this plan
and `qemu/`.** §2 is what was measured. The QEMU build is reproducible:
`qemu/build.sh`.

---

## 1. Decisions

| # | Question | Decision |
|---|---|---|
| D1 | What is emulated | **The real board**: QEMU's `raspi4b` (BCM2711), with [rpi-qemu](https://github.com/fpgas-online/rpi-qemu)'s 41 patches (GENET Ethernet, PCIe root complex, the firmware's device-tree fix-ups, the watchdog Raspberry Pi OS trixie needs to boot at all) and ours (`qemu/trimixxx-patches.py`): HVF, a working PCIe link with xHCI behind it, SD card DMA, the full RAM. Not QEMU's generic `virt` machine, which cannot run the Pi's kernel. |
| D2 | One image | **Byte-identical.** No second kernel, no virtio, no VM-only udev rules. `pi-qemu` does the VideoCore firmware's job on the host — `autoboot.txt`, `config.txt`, `cmdline.txt`, overlays, the MAC in the device tree — and hands QEMU the kernel, DTB and initramfs off the card. |
| D3 | The S3 link | The Pi's own PL011, `ttyAMA0`, made `serial0` by `dtoverlay=disable-bt` and Raspberry Pi OS's own udev rule, exactly as on the deck. QEMU backs it with a Unix socket; the panel (or a real S3 on a USB-UART) is on the other end. |
| D4 | Parameters | **hostname, accent, MAC.** The hostname also picks the deck's hardware files (`mixxx_config/units/<host>.json`, `pi-qemu/decks/<host>.json`), which ship in the image. |
| D5 | MAC | **On a Pi, the board's own.** In an emulated deck, a dummy MAC (`02:54:4d:58:00:NN`, locally administered), written into the device tree's Ethernet node by `pi-qemu` exactly where the Pi firmware writes the real one. The fork announces it on Pro DJ Link and keys peers by it, so each emulated deck gets its own. |
| D6 | trimixxx0 | A virtual trimixxx2: its wiring (the eighth pad, reversed hot cues and jog) and 1280×800 panel, its own accent (`#00C8FF` unless you prefer another) and dummy MAC. |
| D7 | Deck data | **A data partition**, outside the A/B slots: the Mixxx database and analysis, `effects.xml`, `trimixxx-levels`, `~/Music`, ssh host keys, machine-id, Wi-Fi joined on the deck. Root is laid out so it can later be mounted read-only under overlayfs. |
| D8 | Updates | **Flash once, then over the network.** Raspberry Pi's own A/B boot: `autoboot.txt` with `tryboot_a_b=1`, a new version written into the idle slot, tried once with `reboot "0 tryboot"`, kept only if it comes up healthy. The emulated deck honours the same, so an update is tried on trimixxx0 before a deck sees it. |
| D9 | The panel | A local web page: the screen (QEMU's VNC over a websocket) set into trimixxx2's plate, every control and light. Served by `pi-qemu` (Go, one static binary per host OS). |

## 2. What was measured (2026-10-04, M1, 16 GB, QEMU 11.1.0)

The stock **Raspberry Pi OS Lite trixie arm64 (2026-09-15)** card, unmodified
but for a cloud-init user seed on its boot partition:

| | Stock QEMU `raspi4b` | rpi-qemu, TCG | **rpi-qemu + ours, HVF** |
|---|---|---|---|
| Boots | no: refuses HVF; under TCG trixie's initramfs watchdog would reset it in a loop (fixed by rpi-qemu) | yes, 140 s first boot | **yes, 22 s to login** (first boot with cloud-init: ~60 s) |
| CPU (Python loop, 3 M iterations) | — | 2.26 s | **0.17 s** (M1 native: 0.16 s; a Pi 4 ≈ 0.5–0.6 s) |
| Kernel | | `6.18.50+rpt-rpi-v8`, "Raspberry Pi 4 Model B" | same; all 4 cores, booted at EL1 |
| `/dev/serial0` | | → `ttyAMA0`, by Raspberry Pi OS's own rule | same |
| Ethernet | | eth0 = `bcmgenet`, DHCP | same; MAC random until the DT carries one (D5) |
| USB | | DWC2 only (USB 2) | **xHCI on the PCIe root port, like the VL805**: USB audio as card 0 (`snd-usb-audio`, as the UCA222), a stick at 5 Gb/s as `sda`; keyboard and tablet on DWC2 |
| SD card | | PIO | **ADMA**; `dpkg --verify` over every package file: no corruption; ~6 MB/s read |
| RAM | | ~0.9 GiB visible | **1.8 GiB** of the board's 2 GiB |
| Screen | | firmware framebuffer, 1280×800 | same; X on `fbdev`, Mesa 26.2.2 **llvmpipe** (the deck: the same Mesa on V3D) |
| Mixxx 2.5 (stock) | | | **runs**: LateNight skin, GL waveforms, track analysis (120 BPM found), playback to the Mac's speakers; 56 % of one core playing, X 7 % |

Seen along the way: QEMU's user-mode network ran into **Little Snitch**, which
held a new QEMU binary's first UDP packet waiting for a decision and froze the
guest with it (QEMU sends from the vCPU thread). Little Snitch is now off on this
Mac; on a laptop that runs it, allow the rebuilt binary or start with
`--net restricted`.

## 3. Deck and emulated deck, layer by layer

| Layer | Deck | trimixxx0 |
|---|---|---|
| Card image: firmware files, kernel, DTBs, initramfs, root filesystem | the image | **the same image** |
| Boot | VideoCore firmware reads `autoboot.txt`/`config.txt` | `pi-qemu` reads the same files and does the same |
| CPU | Cortex-A72, 1.8 GHz | M1 cores through HVF (`--accurate`: a real Cortex-A72 model under TCG, ~4× slower than a Pi) |
| RAM | 4 GiB | 2 GiB (QEMU's board; 4 GiB is a further patch) |
| S3 link | PL011 on GPIO 14/15 | the same PL011, on a socket |
| Audio | UCA222 on the VL805 | QEMU USB audio on xHCI on PCIe (48 kHz only: see §6) |
| DJ sticks | USB sticks, `dj-usb` | stick images hot-plugged on xHCI, `dj-usb` |
| eth0 | GENET | GENET, with the dummy MAC; unplugged, bridged to the Mac's CDJ port, or shared with other emulated decks |
| Screen | DSI panel through vc4 KMS, X `modesetting`, V3D | firmware framebuffer at the panel's size and depth, X `fbdev`, llvmpipe |
| Touch | Goodix | QEMU USB tablet (absolute pointer — what `deck-poke` already tests with) |
| Not there | | Wi-Fi/Bluetooth, the EEPROM, the backlight, V3D/HVS/DSI |

The honest limit: **trimixxx0 tests behaviour, not performance or the GPU.**
Xruns, frame rate, boot time and temperature are measured on a deck; a V3D
driver bug only shows there.

## 4. `pi-qemu`, the firmware on the host

QEMU's `raspi4b` boots a kernel it is handed; on a Pi, the VideoCore firmware
chooses it. `pi-qemu` does that part, reading the card image directly:

1. `autoboot.txt` on partition 1 → which boot partition (slot A or B, or the
   `[tryboot]` one if a tryboot reboot was asked for).
2. That partition's `config.txt` (with `[pi4]`, `[all]`, `include`, …):
   `kernel=`, `initramfs`, `cmdline=`, `dtparam=`, `dtoverlay=`. Overlays are
   applied with Raspberry Pi's own `dtmerge`, built for the host, so parameters
   work. **Graphics overlays are skipped** (`vc4-kms-*`, the DSI panel): there is
   no V3D/HVS to drive, so the kernel uses the firmware framebuffer, set to the
   deck's panel size and depth (1280×800, 16 bpp: the deck's own `/dev/fb0`
   geometry, so even the boot splash fits).
3. The firmware's device-tree edits: the MAC (D5), and a `/hypervisor` node so
   `systemd-detect-virt` says it is a VM — the one honest marker, for the few
   units that drive hardware the board does not have (the EEPROM check).
4. `cmdline.txt` with the DTB's own bootargs in front and `console=serial0`
   resolved, as the firmware does. (Found the hard way: Raspberry Pi OS's first
   boot changes the card's disk ID and rewrites `cmdline.txt`, so it has to be
   read off the card at every boot.)
5. Reboots: QEMU is run with reboots turned into a pause, so `pi-qemu` sees the
   reboot, reads which partition and whether `tryboot` was asked (a small QEMU
   patch exposes both), and starts the board again from step 1 — A/B updates
   behave as on a Pi.

The rest of `pi-qemu`: the panel (§5), sticks, Ethernet, `ssh`, `console`,
`pack` (below).

## 5. The panel

Unchanged from the first plan in substance. It is a virtual S3 on the Pi's
UART, byte-compatible with `firmwares/trimixxx-midi/src/main.cpp`
(`MidiMap.hpp` generates its table). The page shows the screen and draws
trimixxx2's top plate to scale from `ref_sizes/dims_v2_model.md`, with each pad
bound to a ring node and labelled through the unit file. It covers:

- Mouse, trackpad and keyboard, including chords: PLAY or CUE held through power-on, and the panic chord.
- Pointer on the screen as touch.
- Power on, pull the plug, reset.
- USB sticks in and out, the Ethernet cable in and out.
- Screenshots and a MIDI monitor.
- An actual-size view.

A real S3 on a USB-UART can take the socket instead (`--s3 /dev/cu.…`).

**Sound:** the emulated deck's output goes to the Mac's speakers. The panel
starts muted with a visible switch; `pi-qemu run` takes `--audio
none|speakers|wav:<file>` and defaults to `none` unless the panel is open.

## 6. QEMU work still to do

Carried in `qemu/trimixxx-patches.py`, each small:

| | What | Why |
|---|---|---|
| Q1 | 4 GiB board (revision `c03115`, the firmware's memory layout) | the deck has 4 GiB |
| Q2 | Reboot partition and `tryboot` flag visible over QMP | A/B updates in the emulator (§4.5) |
| Q3 | USB audio presented as the UCA222 (TI PCM2902, "USB Audio CODEC", 44.1/48 kHz) | `soundconfig.xml` then matches unchanged: Mixxx keys the device by name, and QEMU's device is 48 kHz only |
| Q4 | Faster SD reads (multi-block) | ~6 MB/s today; apt and big copies are slow |

Then offer the PCIe, SD, RAM and HVF fixes to rpi-qemu, so there is less to
carry. Portability: the build links Homebrew libraries, so `pack` bundles them
next to the binary (and re-signs it with the hypervisor entitlement); on Linux,
the same script builds against distro packages.

## 7. The card layout (decks and trimixxx0 alike)

| # | Label | FS | Size | Holds | Written by |
|---|---|---|---|---|---|
| 1 | `bootsel` | FAT | 64 MiB | `autoboot.txt`, `trimixxx.conf` (hostname, accent; MAC only in an emulated deck) | flash, the update agent's commit |
| 2 | `boot-a` | FAT | 512 MiB | slot A: firmware, kernel, initramfs, DTBs, overlays, `config.txt`, `cmdline.txt` → `root-a` | flash / update |
| 3 | `boot-b` | FAT | 512 MiB | slot B, the same → `root-b` | update |
| 5 | `root-a` | ext4 | 8 GiB | slot A root (read-only later, overlayfs) | flash / update |
| 6 | `root-b` | ext4 | 8 GiB | slot B root | update |
| 7 | `data` | ext4 | the rest | `/data` (D7) | first boot (made and seeded) |

MBR with an extended partition (4) or GPT: P1 checks which the Pi 4's
bootloader accepts for `boot_partition` beyond 4, and picks.

`/data` is mounted early; `~/.mixxx`, `~/Music`, `/etc/ssh/ssh_host_*`,
`/var/lib/trimixxx` and NetworkManager's own connections live there. On an image
change the identity service refreshes the config files inside `~/.mixxx` from
the image (skin, mapping, `mixxx.cfg`, `soundconfig.xml`) and leaves the state.

## 8. Building the image

It's an emulated Pi that builds it, so there is no Docker chroot and no loop
device:

1. Fetch the pinned Raspberry Pi OS Lite trixie arm64 image and check its
   checksum.
2. Seed cloud-init on its boot partition: the user, your keys, passwordless
   sudo, the build hostname. Cloud-init is the mechanism the image already uses,
   and this was proven in the prototype.
3. Boot it with `pi-qemu` (headless, `--audio none`). Provision it over ssh with
   `provision.sh`, which is `fresh-install.md` §1–2 as code, then the deck's own
   deploy scripts with `HOST=trimixxx-build`. Then the identity service, the
   data-partition mounts, and the update agent.
4. Seal it: identity back to the default, host keys and machine-id gone, caches
   cleared.
5. Inside the same emulated Pi, write the release artifacts from the sealed
   root: `boot.vfat` and `root.ext4` (`mkfs.ext4 -d`), and a whole-card image
   in the §7 layout. Copy them out over ssh with a manifest of every package
   version and every file hash, and `/etc/trimixxx-release`.

| Artifact | For |
|---|---|
| `trimixxx-<v>.img.zst` | the one or two physical flashes per deck |
| `trimixxx-<v>.update` (boot + root + manifest) | network updates (`pi-qemu push <deck>`) |
| the same card, as a `.qcow2` | an emulated deck (`pi-qemu create trimixxx0`) |

## 9. Updating a deck over the network

`pi-qemu push trimixxx2 trimixxx-<v>.update`:

1. Streams the bundle over ssh to `trimixxx-update` on the deck.
2. That writes the idle slot's boot and root, points its `cmdline.txt` at its
   own root, then runs `reboot "0 tryboot"`.
3. On the tried boot, a health check confirms the MIDI bridge, the launcher's
   mode, Mixxx's controller and the sound card. Only then does it rewrite
   `autoboot.txt` to make the new slot the default.
4. Anything less, including a hang (the hardware watchdog catches that), and the
   next boot is the old slot.

The same command against `trimixxx0` tries the update in the emulator first.

## 10. Phases

| Phase | Builds | Done when |
|---|---|---|
| P0 | Capture what only lives on the decks (trimixxx1's `config.txt`/panel, both MACs, `apt-mark showmanual`, `asound.state`) | the repo can recreate both decks |
| P1 | `qemu/build.sh` (done), `pi-qemu run` with the firmware emulation of §4.1–4 | the stock card boots to login from `pi-qemu run`, at 1280×800, muted |
| P2 | The deck stack in trimixxx0 through today's scripts (`HOST=trimixxx0`) | the TriMixxx skin, the bridge on `ttyAMA0`, `deck-poke`/`deck-shot` work |
| P3 | The panel | a set played by hand, boot gestures, POWER |
| P4 | Identity at boot, the §7 layout, the data partition | one card comes up as trimixxx0 or trimixxx2 from `trimixxx.conf` alone |
| P5 | The image pipeline (§8) and Q1–Q3 | an image built with no deck attached passes P3's set |
| P6 | Flash once, `pi-qemu push`, the update agent, tryboot in the emulator (Q2) | trimixxx2 runs a flashed card, then takes an update over the network; then trimixxx1 |
| P7 | Optional: several emulated decks on one virtual Pro DJ Link network; end-to-end tests driving the panel over QMP | — |

## 11. Risks

- **GPU.** llvmpipe instead of V3D; graphics overlays skipped (§4). Capping
  Mesa at OpenGL 3.1 in the emulator catches "needs more than the Pi has".
- **CPU.** The M1 is newer than the A72. `--accurate` (TCG with the real A72
  model) is there to catch an illegal instruction, slowly.
- **Out-of-tree QEMU.** rpi-qemu's series and ours are not upstream. Both are
  pinned, and the build fails loudly if a bump moves the code.
- **The first flash moves the decks to the A/B layout.** Back up
  (`pi-qemu backup`) first, and use a spare card for the first one.
- **Secrets** (Wi-Fi password, keys) are inside the image, so images stay
  private.
- **Little Snitch** or any outbound filter on a host (§2).
