# Fresh install — from Raspberry Pi OS Lite to a working deck

The order to bring a deck Pi up from a freshly flashed card, written while doing
it for **trimixxx2** (started 2026-10-01). Trimixxx1 was away and unreachable,
so everything here comes from this repo and from the new unit itself — which is
also how the state that only ever lived on Trimixxx1 was found. It is listed at
the end.

Generic Raspberry Pi setup first, TriMixxx after. Each step records what was
done on trimixxx2 and what it showed.

`[x]` done on trimixxx2 · `[ ]` to do · `[~]` skipped or deferred, with why

> **There is no default deck.** Every `pi-qemu` command here names its deck: a
> real one is `--host` and its ssh alias, `--host trimixxx-pi-2` for trimixxx2
> and `--host trimixxx-pi` for Trimixxx1. The commands below say
> `trimixxx-pi-2`, the deck this was written on. On a new deck, use its own
> alias. `pi-qemu help` lists the commands.
>
> `pi-qemu deck deploy --host ALIAS` runs every deploy step, in this document's
> order: `base` (most of §1–2), `ttymidi` (§3.1), `system` (§3.2), `launcher`
> (§3.3), `mixxx`, `config` and `library` (§3.4), then `doom` (§3.5). Naming
> steps after the alias runs only those. Passwordless sudo (§1.1) comes first,
> by hand. trimixxx2 was set up with the scripts these steps replaced; §6 names
> them.

## Where it stands

Hardware finished and the case closed (panel, touch and the S3 all wired).
**The deck runs**: the TriMixxx skin on the panel, the controller live over the
UART both ways, the UCA222 at full output. Done: §1, §2, §3.0–3.6 (bar the
UCA222's MONITOR switch, hardware). The deploy-script fixes it took are in §6.
Since 2026-10-01 the splash comes up much sooner after power-on (§1.4) and the
button lights go off at shutdown (`trimixxx-lights-off`, see the README).

Since then: normalization is off (+6 dB on every track, §3.6), and this deck's
different button wiring is remapped in Mixxx (§3.4).

**Left:**
1. Pad-by-pad check of the remap, by hand (§3.4).
2. Reseed the effects rack, which is empty (§3.4).
3. The final check (§4); several of its items need hands on the deck.

## 0. Starting point

Raspberry Pi OS Lite, trixie, flashed with Raspberry Pi Imager (user `sam1902`,
hostname `trimixxx2`, home wifi, ssh key), then `apt update && apt upgrade`.
Nothing else. What the fresh image looked like:

| | |
|---|---|
| Board | Pi 4 Model B **Rev 1.5**, 4 GB |
| OS / kernel | Debian 13 trixie, `6.18.50+rpt-rpi-v8` aarch64 |
| Card | 29 GB root (Trimixxx1: 58 GB) |
| `/tmp` | tmpfs, 1.9 GB — stock, same as Trimixxx1 |
| Swap | 2 GB zram via `rpi-swap` — stock, same as Trimixxx1 |
| Boot | 49.5 s to the end of userspace, **26 s of it cloud-init** |
| Sound cards | 0 bcm2835 Headphones · 1–2 vc4hdmi · **3 UCA222** (`CODEC`, PCM2902) |
| UART | no `/dev/serial0`; serial console still on the kernel command line |
| sudo | **not** passwordless: `(ALL : ALL) ALL` |
| ssh | key login works (Imager); password login still on |
| Wi-Fi power save | on |

## 1. Generic Raspberry Pi setup

### 1.1 Access
- [x] **ssh key login.** Done at flash time; `ssh trimixxx-pi-2` works with the
      key `~/.ssh/config` names.
- [x] **Passwordless sudo.** Every deploy step runs `sudo` on the deck over ssh
      with no terminal, so a password prompt is a hard failure, and Raspberry
      Pi OS no longer ships the old `010_pi-nopasswd`. Installed
      `/etc/sudoers.d/010_sam1902-nopasswd` (`sam1902 ALL=(ALL) NOPASSWD: ALL`),
      checked with `visudo -cf` before it went in. The same line was also added
      to `/etc/sudoers` (line 48) by hand; either one alone is enough.
- [ ] **`PasswordAuthentication no` for sshd** — no longer optional once the
      Wi-Fi fallback is in (§3.8): its password is in the repo, so anyone at a
      venue can join the deck's hotspot, and with password logins on, the
      login password is all that stands between them and a passwordless sudo.
      `authorized_keys` holds two keys, and key login works. A drop-in,
      `/etc/ssh/sshd_config.d/100-custom-sshd.conf`, was written by hand on
      2026-10-01 but says `PasswordAuthentication yes`, so nothing changed and
      sshd was not restarted. When it says `no`, it wins: sshd keeps the first
      value it reads, and `100-…` sorts before cloud-init's `50-…`, which also
      says `yes`. Validate with `sudo sshd -t`, check `sudo sshd -T | grep
      passwordauthentication`, restart, and prove a NEW key login works before
      closing the old session. The base step now writes `05-trimixxx.conf`
      saying `no`, which sorts before both; it applies from sshd's next start.

### 1.2 Firmware
- [x] **Bootloader EEPROM: network install off** (and with it, the update to
      2026-09-23; the 2026-01-09 image was no longer on disk to keep). Stock Pi 4
      EEPROMs set `NET_INSTALL_AT_POWER_ON=1`, which on every **cold** boot
      shows a "hold SHIFT for network install" screen — on HDMI only, so the
      panel just stays black — and it was most of the black screen at power-on:
      the SD card was first touched **8.15 s** after power came up. Done with:
      ```sh
      sudo rpi-eeprom-config > /tmp/boot.conf    # then edit it to read:
      # NET_INSTALL_AT_POWER_ON=0
      # NET_INSTALL_ENABLED=0     <- also skips the ~1 s USB keyboard probe
      sudo rpi-eeprom-config --apply /tmp/boot.conf && sudo reboot
      ```
      The other lines (`BOOT_UART=0`, `WAKE_ON_GPIO=1`, `POWER_OFF_ON_HALT=0`)
      stay as they were. The flash happens on that reboot, from `recovery.bin` on
      the boot partition, which stays there until the flash succeeds — a power cut
      mid-flash just means it flashes again on the next boot. Keyboard detection
      at boot is also known to leave some already-connected USB devices stuck
      ([rpi-eeprom#834](https://github.com/raspberrypi/rpi-eeprom/issues/834));
      the UCA222 is one of those. Check: `vcgencmd bootloader_config`.

### 1.3 Boot flags — `/boot/firmware/config.txt`, `cmdline.txt`
Originals kept as `config.txt.pre-trimixxx` and `cmdline.txt.pre-trimixxx`
next to them. Three stock lines changed in place, and one block appended at the
end of `config.txt`:

```
dtparam=audio=on          ->  dtparam=audio=off
display_auto_detect=1     ->  display_auto_detect=0
dtoverlay=vc4-kms-v3d     ->  dtoverlay=vc4-kms-v3d,noaudio
camera_auto_detect=1      ->  camera_auto_detect=0     (§1.4, boot speed)

[all]
enable_uart=1
dtoverlay=disable-bt
dtoverlay=vc4-kms-dsi-waveshare-panel,10_1_inch
disable_splash=1
```

and in `cmdline.txt`, `console=serial0,115200 ` removed (it stays one line).
- [x] **UART for ttymidi:** `enable_uart=1` + `dtoverlay=disable-bt`, so
      `/dev/serial0` is the full PL011 (`ttyAMA0`) on GPIO14/15, whose baud does
      not drift with the core clock. Bluetooth goes; the deck has no use for it.
- [x] **Serial console off:** `console=serial0,115200` removed from
      `cmdline.txt`, which would otherwise fight ttymidi for the port.
- [x] **One sound card:** `dtparam=audio=off` and `dtoverlay=vc4-kms-v3d,noaudio`
      (HDMI *video* stays). The UCA222 becomes card 0, as on Trimixxx1 —
      `soundconfig.xml` says `hw:0,0`, although Mixxx matches on the device name
      when only one device has it.
- [x] **The panel:** `dtoverlay=vc4-kms-dsi-waveshare-panel,10_1_inch` — Waveshare
      10.1" DSI LCD (C), 1280×800, Goodix touch over I2C0 *through the DSI
      ribbon*; the GPIO header only powers it (5V/GND). Wiring the panel's
      SDA/SCL to GPIO2/3 instead needs `,i2c1` on the overlay.
      `display_auto_detect=0`: Waveshare panels answer on the official Touch
      Display's I2C address, and auto-detect would load the wrong overlay on top.
- [x] `disable_splash=1` — no rainbow square; the deck's own splash follows.
- [x] Console blanking: `consoleblank` is already 0 on this kernel. Nothing to add.
- [x] Verified on the first boot with the panel: `/dev/serial0` → `ttyAMA0`;
      `aplay -l` lists only `CODEC`, as card 0; `DSI-1` connected, HDMI not;
      `/dev/fb0` 1280×800, 16 bpp, stride 2560; backlight `10-0045` at 255/255;
      `Goodix-TS 10-0014: ID 9271` registered as an input device.

### 1.4 Boot time and background load
- [x] **cloud-init off**, now that Imager's first boot is done:
      `sudo touch /etc/cloud/cloud-init.disabled`. The 26 s was first-boot
      provisioning; on later boots it was ~2 s, and it orders itself before
      `systemd-user-sessions`, which the tty1 autologin — hence Mixxx — waits on.
      Wifi is unaffected: the connections live in `/etc/netplan` (NetworkManager's
      own `90-NM-*.yaml`). `cloud-init status` → `disabled`; ssh came back.
- [x] `sudo systemctl disable --now bluetooth` — no controller left after
      `disable-bt`.
- [x] `sudo systemctl disable --now apt-daily.timer apt-daily-upgrade.timer
      man-db.timer` — no background apt or man-db runs mid-set competing with
      the audio thread for CPU and card IO. Update by hand instead.
- [x] **Wi-Fi power save off:** `/etc/NetworkManager/conf.d/wifi-powersave-off.conf`
      with `[connection]` / `wifi.powersave = 2`, and live with
      `sudo iw dev wlan0 set power_save off` (no reconnect). Still off after the
      reboot.
- [x] Reboot: whole boot **15.3 s** (22.9 s before this section, 49.5 s fresh).
      `getty@tty1` starts at 13.0 s, behind `NetworkManager.service`, which takes
      7.8 s to start — where most of what is left goes.

#### Splash as soon as possible after power-on
The panel used to stay black for ~20 s after power-on before the TriMixxx logo.
Where that went, and what was done (times from power-on unless said otherwise):

| Stage | Was | Fix |
|---|---|---|
| Bootloader's network-install screen, cold boots only | 8.15 s | EEPROM, §1.2 |
| Firmware reads the 12.5 MB initramfs (SD at ~10.6 MB/s) | 1.18 s | kept — see below |
| Firmware reads and un-gzips the kernel | ~2.3 s | — (an uncompressed one reads no faster) |
| systemd generators: `rpi-swap` runs Perl (`ack`) twice | 1.37 s | swap sizes pinned |
| vc4 waits for the panel's drivers from the root fs; panel up at 6.15 s kernel time | | panel drivers in the initramfs |

- [x] **Panel drivers in the initramfs.** Raspberry Pi's initramfs already
      carries vc4 and starts the display there, but vc4 binds the panel and both
      HDMI ports as one unit, and the DSI panel's driver and the I2C mux in front
      of it were not in it — so the panel waited for the root filesystem.
      The system step adds `i2c_mux_pinctrl` and `panel_waveshare_dsi` to
      `/etc/initramfs-tools/modules`, the standard place, which every kernel
      update re-reads, and rebuilds with `sudo update-initramfs -u -k "$(uname -r)"`
      (that also copies it to `/boot/firmware/initramfs8`). The panel now comes
      up at **1.66 s** kernel time, showing the boot log until the logo.
- [x] **The initramfs stays.** Booting without one (`auto_initramfs=0`) was tried
      and works — this kernel has the SD and ext4 drivers built in — and is
      ~1.5 s faster to the logo, but it was **reverted**: with the kernel mounting
      root itself, root shows as `/dev/root`, and `update-initramfs` (with
      `MODULES=dep`) then fails with *failed to determine device for /*. Every
      kernel update runs it, so the next `apt upgrade` would have ended in a
      dpkg error; and Raspberry Pi OS ships with the initramfs on, so nothing
      stops a future kernel from moving those drivers into modules, which would
      leave a deck without one unbootable. Not worth a second.
- [x] `camera_auto_detect=0` — no camera; spares the firmware probing for one.
- [x] **Swap sizes pinned:** the system step installs `trimixxx-swap-sizes.conf`
      into `/etc/rpi/swap.conf.d/` (2048 MiB zram, 2048 MiB file — what
      rpi-swap was computing anyway). Checked: the generator writes
      byte-identical units. Generators 1.37 s → 0.70 s, and no unit starts
      before they finish.
- [x] Result, cold boot (2026-10-02, `rsts` 0x1000), firmware clock from
      power-on: SD card first read at **2.8 s** (8.15 s before), kernel starts at
      **8.2 s** (14.0 s), panel shows the boot log at ~9.9 s (kernel 1.66 s; was
      ~20 s) and the logo at ~13.1 s (kernel 4.84 s; was ~20.5 s). The logo waits
      for systemd, because the splash is a systemd service. Counted by hand from
      the power going on: ~8 s to the splash.
- **Trying a boot change safely** on a deck you cannot get a keyboard into — how
  all of the above was tested: put the candidate in `/boot/firmware/tryboot.txt`
  (an initramfs as `initramfs <file> followkernel` with `auto_initramfs=0`; and
  `cmdline=cmdline.tryboot.txt` pointing at a copy of `cmdline.txt` that adds
  `rootwait=20 panic=10`, so a root that never mounts reboots by itself), then
  `sudo reboot '0 tryboot'`. Only that one boot uses it; any reboot or power
  cycle after returns to `config.txt`. `/proc/device-tree/chosen/bootloader/tryboot`
  reads 1 during the test.
- Measuring: `sudo vclog --msg` is the firmware's own log, stamped in ms from
  power-on (`arasan_emmc_open` = SD first touched, `Starting ARM` = kernel
  starts). After that, use `dmesg` — the splash writes
  `trimixxx-splash: up on vt7` there. Do **not** use journal times this early:
  journald is not up yet and stamps lines when it reads them, seconds late.
  `rsts` in `/proc/device-tree/chosen/bootloader/` says which kind of boot it
  was: `0x1000` power-on, `0x20` reboot.

### 1.5 Card wear
- [x] **log2ram:** `sudo apt install log2ram` — Debian trixie packages it now
      (1.7.2), no third-party repo, no other packages pulled in. `/var/log` (576 K
      at the time) moves to a 128 M tmpfs and syncs to the card daily
      (`log2ram-daily.timer`) and at shutdown. Trimixxx1 ran it. Active after the
      reboot: `findmnt /var/log` → `log2ram tmpfs`.
- [~] Cap journald to fit in log2ram's RAM disk — not needed here: this image
      has no `/var/log/journal`, so the journal is volatile and already lives in
      `/run`, not in `/var/log`.
- [x] **Mixxx's logs off the card** (2026-10-02). They were the card's one
      steady writer, not `/var/log`: with Mixxx idle, 33 writes a minute, and
      112,546 of the 113,212 lines in a session's `mixxx.log` were a single
      `--developer` message. log2ram does not cover `~/.mixxx`. `xinitrc` now
      runs Mixxx without `--developer` and puts both logs in `/tmp/mixxx`
      (tmpfs) — `mixxx.log` through the fork's `--log-path`. They no longer
      survive a reboot.
- [x] **Mixxx never writes `mixxx.cfg`** (fork, 2026-10-02): it used to save
      it on exit by deleting the old file and renaming the new one in, with no
      fsync, so a power cut within ~30 s of quitting could leave it empty.
      The config step is now the only thing that writes it.

### 1.6 Real-time audio — only if needed
- [ ] If the first Mixxx run logs that it could not get real-time priority:
      `/etc/security/limits.d/95-audio.conf` with `@audio - rtprio 95` and
      `@audio - memlock unlimited`. Not known whether Trimixxx1 had it.

## 2. Display, touch, X

- [x] **Packages**, run in a transient systemd unit so a dropped ssh cannot
      leave dpkg half-done: `xserver-xorg xinit x11-xserver-utils xinput
      libgl1-mesa-dri mesa-utils unclutter picom matchbox-window-manager scrot
      xdotool evtest libinput-tools mixxx`. That is 337 packages with recommends,
      and no PipeWire, PulseAudio or display manager among them (any of those
      would take the UCA222 away from Mixxx). Recommends also brought
      `gnome-keyring` and `gcr-ssh-agent`: socket-activated user units that
      `startx` never wakes. `scrot` / `xdotool` are for `pi-qemu deck shot` and
      its touch verbs (`deck tap`, `swipe`, ...); `evtest` / `libinput-tools` /
      `mesa-utils` are for checking the panel.
- [x] **`xinitrc`, two fixes** (this repo, §6): the DSI panel is the primary
      output, an HDMI monitor mirrors it scaled to fit (debugging), and the
      touchscreen is pinned to the panel; and `mkdir -p ~/.mixxx` before the
      Mixxx loop. Its `awk` parsing was checked against sample `xrandr` /
      `xinput` output under the Pi's own `mawk`.
- [x] Session files onto the Pi: `~/.xinitrc`, `~/.bash_profile`,
      `/usr/local/bin/trimixxx-debug` — the session part of what was then
      `pi_config/upload.sh`, run on its own so the panel could be tested before
      the rest of the deck existed. The system step installs them now.
      There was no previous `~/.bash_profile`, and `~/.profile` has no `startx`.
- [x] **Console autologin on tty1:** `sudo raspi-config nonint do_boot_behaviour B2`
      — writes `getty@tty1.service.d/autologin.conf`
      (`agetty --autologin sam1902`). The base step does this now; Trimixxx1
      had it done by hand. Until the rest of the deck is installed, the session
      starts stock Mixxx 2.5.0 with its first-run dialogs — enough to test touch.
- [x] Powered off to wire the panel (2026-10-01).
- [x] Verified with the panel attached: `xrandr` shows `DSI-1` 1280×800 at
      60 Hz, primary; the touchscreen's coordinate matrix is the identity (the X
      screen is exactly the panel); touch confirmed working by hand; `glxinfo -B`
      → `V3D 4.2.14.0`, direct rendering, OpenGL 3.1 Mesa 26.2.2. Stock Mixxx came
      up on it, showing its first-run "Choose music library directory" dialog.
- [ ] **Orientation**, once it is in the chassis: if it mounts upside down,
      `rotation=180` on the panel overlay, plus the touch `invx`/`invy` the
      overlay README describes — re-check after.
- [x] **DECIDED — scale Mixxx to fit the panel** (`xinitrc`, §6). The skin is
      pinned to 1024×600: every top-level group in `skin.xml` has Minimum =
      Maximum `1024,600`, so on this panel it would sit top-left with a 256 px
      band to the right and 200 px below. `xinitrc` now exports
      `QT_SCALE_FACTOR` = the smaller of panel-width/1024 and panel-height/600,
      only when that is not 1: **1.25** here, giving a 1024×640 logical screen —
      the skin fills the width and leaves 40 logical (50 real) px at the bottom.
      Nothing changes on a 1024×600 panel. The variable overrides `mixxx.cfg`'s
      `ScaleFactor` (`adjustScaleFactor()` in `mixxx/src/main.cpp`), so one
      shared `mixxx.cfg` still serves both decks. Text stays sharp, bitmap art is
      resampled, and the GL waveforms draw ~56 % more pixels — watch the frame
      rate and temperature. Options not taken: also letting the skin's height
      stretch into the last 40 px (a skin change, possible later); scaling in X
      (`xrandr --scale-from 1024x600`: a 6.7 % vertical stretch, soft, a GPU
      transform every frame); a native 1280×800 layout (the most work).

## 3. TriMixxx

### 3.0 On the Mac
`pi-qemu` on PATH (`pi-qemu/build.sh` puts it there), Docker Desktop running
(all Pi binaries are arm64 builds in Docker), `uv`, and `rsvg-convert`
(`brew install librsvg`) for the splash.
- [x] Built while the Pi was off, so the deploys below hit a warm cache:
      `ttymidi`, `trimixxx-launchd` and `trimixxx-deckkeys` (static arm64), and
      the Mixxx fork at HEAD `08174e6` (12 min). The `mixxx/dist/mixxx` on disk
      predated that commit. Each deploy rebuilds anyway, from that cache.

### 3.1 ttymidi — before the system step
- [x] `pi-qemu deck deploy --host trimixxx-pi-2 ttymidi`
      → `/usr/local/bin/ttymidi`, `ttymidi --version` → `42175e8`.

  First because the system step enables and *restarts*
  `trimixxx-bridge.service`, which needs this binary. The system step checks
  for it and stops before changing anything if it is missing (§6). On trimixxx2
  this was ttymidi's `make install-remote`, which needed `SERVICE=` (empty) on
  a fresh unit: it ended with `systemctl try-restart trimixxx-bridge.service`,
  which exits 5 for a unit that does not exist yet, so `make` reported a
  failure after the binary was in fact installed. The ttymidi step restarts the
  bridge only if it runs.

### 3.2 The deck's system (`pi_config/`)
- [x] `pi-qemu deck deploy --host trimixxx-pi-2 system`. On trimixxx2 this was
      `pi_config/upload.sh`: exit 0, end to end, its first complete run since
      2026-08-01. Fixed before first use here (§6):
  - **It did not parse.** An apostrophe in an `echo` inside a single-quoted
    `ssh '…'` block closed the quote, so every run since that line went in died
    after `~/.xinitrc` — the `.bash_profile`, getty drop-in, splash, eth0 and
    dj-usb sections never ran from it.
  - **eth0 with no cable** no longer fails the run: the profile is still set to
    link-local, and NetworkManager brings it up when something is plugged in.

  What it did: governor `performance` (1.8 GHz, §3.7); `trimixxx-bridge` active
  with the `TriMixxx` ALSA client; `ip_unprivileged_port_start` = 111; the Noto
  chain resolves (Noto Sans → CJK JP → Symbols 2 → Color Emoji → DejaVu); the
  session files, now with the scale factor; the getty stop drop-in; the splash;
  eth0 `auto` → `link-local` (no cable yet, wlan0 untouched); dj-usb.
- [x] **The splash is rendered for whatever `/dev/fb0` is at the time** — with an
      HDMI monitor attached, the monitor's geometry, which the boot-time check
      then refuses (by design). Rendered here with only the panel attached:
      1280×800, 16 bpp, stride 2560, 2 048 000 bytes, matching. Deploy the
      system step again if the panel or its mode changes.
- [x] **The UART link to the S3, both ways.** Pi → S3: three Note On/Off pairs
      for the PLAY LED went out — `tx` in `/proc/tty/driver/ttyAMA` went 0 → 18
      bytes — and after the move the PLAY LED was seen blinking at boot, which
      is the launch manager opening its boot-gesture window over the same path.
      S3 → Pi: `aseqdump -p TriMixxx` showed PLAY (note 60, `0x3C`), CUE (61,
      `0x3D`) and the jog (CC 17, relative ±1) while they were pressed; `rx`
      went 6 → 647 bytes. Note that ttymidi has *two* ports named `TriMixxx`:
      port 0 is read-only, so `aseqsend -p TriMixxx` goes nowhere; the writable
      one is `128:1` (`aseqsend -p 128:1 90 3C 7F`). `aseqdump -p TriMixxx`
      listens on port 0, which is the right one.

### 3.3 Launch manager
- [x] `pi-qemu deck deploy --host trimixxx-pi-2 launcher` — active, both
      readiness gates passed (`pi-midi-daemon` port, mode file), mode `mixxx`.

### 3.4 Mixxx
- [x] `apt install mixxx` (2.5.0+dfsg, trixie) — for its libraries and
      `/usr/share/mixxx`. Only the binary gets replaced.
- [x] `pi-qemu deck deploy --host trimixxx-pi-2 mixxx` — builds the fork and
      swaps `/usr/bin/mixxx` (24 MB), keeping apt's as `/usr/bin/mixxx.apt`
      (17 MB). The `ldd` check found nothing missing. The running Mixxx has
      `QT_SCALE_FACTOR=1.25` in its environment, from `xinitrc`.
- [x] **Once, before the first config deploy:**
      `ssh trimixxx-pi-2 'sudo systemctl stop getty@tty1 && rm -f ~/.mixxx/effects.xml && mkdir -p ~/Music'`.
      Stock Mixxx ran at the first boots and writes its own `effects.xml` *on
      exit* — so stop the session first, or it writes it straight back.
      `mixxx_config/upload.sh` then seeded the TriMixxx chain only when the
      deck had none, so it would otherwise have kept the stock rack, and the
      effect-pedal return would have been silent. `~/Music` is for the next
      point. On trimixxx2 there turned out to be no `effects.xml` to remove:
      Mixxx had never got past its first-run dialog, so it never wrote one.
      Keep the step anyway — it costs nothing.
- [x] `pi-qemu deck deploy --host trimixxx-pi-2 config` — now creates
      `~/.mixxx/skins` and `~/.mixxx/controllers` itself (§6). Every XML
      validated; the shipped `effects.xml` was seeded.
- [x] **Answer "Choose music library directory" once, by touch: Choose → `Music`.**
      Done by hand; the `directories` table now holds `/home/sam1902/Music`.
      Mixxx then came up with the TriMixxx skin, scaled to the panel's width; the
      TriMixxx controller opened for input and output with its script; the
      UCA222 opened as `hw:0,0` at 44.1 kHz, 256 frames (5.8 ms).
      Mixxx shows that dialog at every start for as long as its database has no
      music directory (`loadRootDirs().isEmpty()` in `coreservices.cpp`) —
      Cancel just brings it back next boot. It is database state, not
      `mixxx.cfg`, which is why Trimixxx1 never shows it. On the fresh image
      `~/Music` did not exist, which is why "Choose" was greyed out. The library
      step now does both (`pi-qemu deck deploy --host trimixxx-pi-2 library`):
      it makes `~/Music`, and puts it in the `directories` table if that is
      empty.
- [ ] Check Preferences → Controllers: **TriMixxx** and **pi-midi-daemon** both
      enabled with their mappings. `mixxx.cfg` binds them by device name, so the
      config step should already have done it.
- [x] **This deck's button wiring** (`mixxx_config/units/trimixxx2.json`). BACK
      seemed dead: pressing every control in order with `aseqdump -p TriMixxx`
      running showed ring A has an **eighth pad, third in its chain** (note
      `0x02`, no function yet), which pushes 8-beat, 4-beat, ×2, ÷2 and BACK one
      node down (`0x03`–`0x07`) — so ÷2 was opening the library and BACK sent a
      note nothing listened to — and **hot cues 1–4 arrive reversed**
      (`0x46`…`0x43`). Tempo range, keylock, slip, sort, loop in/out, reloop and
      the encoder are standard; PLAY/CUE too (the capture had them the other way
      round — taken as pressed in the other order). Fixed in Mixxx, not the S3:
      the config step renumbers the mapping from that file for this deck alone
      (`units/apply.py`, see `mixxx_config/README.md`), the ring LEDs follow,
      and Mixxx logs `TriMixxx: buttons and lights remapped for trimixxx2`.
      BACK on its new note opens the library. Note the encoder push over the
      deck is the FX mute, by design — BACK opens the library.
- [ ] By hand on the deck, now remapped: BACK opens the library and a hold
      (0.6 s) returns to the deck; 8/4-beat loops, ×2/÷2 and hot cues 1–4 do
      what they say (needs a track loaded); each pad's light sits on its own
      pad; the eighth pad does nothing.
- [x] **The jog is wired the other way round** on this deck: `"jogReversed":
      true` in the same unit file; `TriMixxx.jog` flips the tick before scratch
      and bend alike. Checked in node (one tick: −0.3 standard, +0.3 here) and
      deployed.
- [x] **Ring LEDs 20 % dimmer** — `TriMixxx.RING_LEVEL = 0.8`, applied in
      `ringLed()`/`ringLedPair()` to everything, so SORT/KEY SYNC (which set
      their own levels, up to 1.0) and the boot sweep dim with the rest and the
      tuned ratios hold. First as 0.8 of the duty cycle, which looked like no
      change: the WS2812s are linear and nothing between Mixxx and them corrects
      for gamma, so 80 % duty reads as ~10 % dimmer. Now gamma-corrected, 0.8²·²
      = 61 % duty (full white 255 → 156). Shared: Trimixxx1 gets it on its next
      config deploy.
- [x] **No bezel strips on this deck** — `"bezel": false` in the unit file. The
      panel sits flush, so the strips Trimixxx1 needs (deck view 36 px top /
      14 px bottom; browser 36 top, 56 bottom, 16 left; rack 56 bottom) were
      dead bands here, plus 20 px of letterbox top and bottom because the skin
      was pinned to 600 high in a 640-high screen. Skin: a `[TriMixxx],bezel`
      attribute the strips follow, and open maximum heights (minimums kept, so a
      600-high screen lays out as before). Fork: `deckbezel.h`, read by
      `WDeckBrowser` and `WDeckRack` when built. Checked by screenshot: deck
      view, browser and Effects page all edge to edge; the waveform grew from
      414 to 488 logical px. Needs the fork rebuilt — `mixxx/dist/mixxx`
      `760d69ef…`, from uncommitted fork sources as of this writing.
- [ ] **Docker's disk is full.** `mixxx/upload.sh`'s rebuild of that same
      binary failed with `No space left on device`; it was installed by hand
      from the earlier, identical build instead (the post-build half of
      `upload.sh`: `ldd` check, swap, session restart). `docker system df`: 28 GB
      of build cache (19 GB reclaimable), 11 GB of images (10 GB reclaimable).
      Pruning the build cache costs the next Mixxx build its ccache — one cold
      ~12 min build.
- [~] **Known limit of a Mixxx-only remap:** the launch manager and Doom
      (`trimixxx-deckkeys`) read the same MIDI with the canonical table. PLAY,
      CUE and LOOP IN/OUT are standard here, so the boot gestures and the panic
      chord work; in Doom, ring A's pads from the third on and ring B's hot-cue
      pads land as wired, and the jog turns the other way.
- [ ] **The effects rack is empty.** The seeded `effects.xml` carries a reverb
      on the pedal bus (EffectUnit2, WET), but the deck's file now holds an
      empty WET chain, and the Effects page shows "EMPTY RACK". Most likely
      emptied by the button test — BACK re-opened the library on the Effects
      page, where pads and the encoder edit the rack. To reseed:
      `ssh trimixxx-pi-2 'sudo systemctl stop getty@tty1 && rm ~/.mixxx/effects.xml'`
      then `pi-qemu deck deploy --host trimixxx-pi-2 config`.

### 3.5 Doom
- [x] `pi-qemu deck deploy --host trimixxx-pi-2 doom` (the WAD is already in
      `doom/wad/`) — Chocolate Doom 3.1.0, 17 packages. Among them are
      `libpulse0` and `libpipewire-0.3`, the client libraries SDL2 links
      against; the PulseAudio and PipeWire *servers* are only suggested and are
      not installed.

### 3.6 Audio levels
- [x] **UCA222 output to 0 dB.** It came up at −20 dB (108/128). Anything under
      unity throws away 16-bit resolution:
      `amixer -c CODEC sset PCM 0dB && sudo alsactl store` → 128/128, 0.00 dB,
      saved in `/var/lib/alsa/asound.state`, which is not versioned (see
      README, "Not yet versioned here").
- [ ] **UCA222 MONITOR switch OFF** — hardware. Left on, it feeds the input
      straight back into the output (`worklog/effect-pedal`).
- [x] **DECIDED — normalization off** (`ReplayGainEnabled 0` in the shared
      `mixxx.cfg`, live on trimixxx2, and on Trimixxx1 since its config upload
      of 2026-10-03). Why, from the investigation:
- **Every track played 6 dB down, on both decks.** Trimixxx1's
      line level was reported as "a bit low". The hardware is already at its
      ceiling: `PCM` 128/128 is the codec's maximum (0 dB, no positive gain),
      Mixxx opens `hw:0,0` with no ALSA software volume in between, and the
      UCA222's line out tops out at **2 dBV (≈1.26 V RMS)** per its manual
      (`datasheet/UCA222-User-Manual.pdf`) — a CD player's line out is
      typically ~2 V RMS (+6 dBV), so ~4 dB more. But `mixxx.cfg` has
      `[ReplayGain] ReplayGainEnabled 1` with `ReplayGainAnalyserEnabled 0` and
      `InitialDefaultBoost -6`: in `EnginePregain::process()` a track with no
      ReplayGain value gets `[ReplayGain],DefaultBoost`, i.e. −6 dB, and with the
      analyser off no track ever gets a value unless its file carries ReplayGain
      tags (rekordbox exports generally do not). So everything plays at half
      amplitude, and nothing is actually being normalised. Options:
  - `ReplayGainEnabled 0` — every track at unity, like a CDJ: +6 dB across the
    board. A loud master's own peaks then reach the DAC as mastered; EQ boosts
    can clip, as on any player at unity.
  - `InitialDefaultBoost 0` — the same for untagged files, but files that carry
    ReplayGain tags would still be normalised down (typically 6–10 dB for modern
    masters), so tagged and untagged tracks would play at different levels.
  - The remaining ~4 dB below a CD player is the UCA222's analog ceiling: the
    mixer's channel trim, or a hotter DAC. Software gain past unity (pregain or
    main gain) only clips loud masters.
- [x] **Diagnostics → Adjust** (fork, `DeckLevels`), the page's first section:
      the output trim — `[Master],gain`, ±6 dB in 0.5 dB steps, with a
      Clipping row beside it for when above unity starts to bite — and the
      panel's brightness, 10–100 % in 5 % steps through
      `/sys/class/backlight/10-0045/brightness`. That file is group `video`
      (Raspberry Pi OS's `60-backlight.rules`) and `sam1902` is in `video`, so
      there is nothing to install. Both are kept in `~/.mixxx/trimixxx-levels`
      and put back when the skin loads. Seen on trimixxx2's panel 2026-10-03
      through `deck-poke` / `deck-shot`: press to adjust, turn, press on to
      brightness, BACK out of adjusting, BACK held mid-adjust, and a restart
      restoring both. Left at 0 dB / 100 % with no levels file.

### 3.7 Per-unit differences
- **CPU clock.** A Rev 1.5 board with `arm_boost=1` runs to 1.8 GHz, so the
  `performance` governor pins 1.8 GHz — measured, `scaling_cur_freq` 1800000 —
  not the 1.5 GHz that `cpu-governor.service`'s comment measured on Trimixxx1.
  In the closed case: 47 °C idle on `ondemand`, 53.5 °C minutes after the
  switch. Trimixxx1 idled at 71 °C in its chassis. Check `vcgencmd
  measure_temp` and `get_throttled` under a real set; `arm_boost=0` is the
  fallback.
- **Pro DJ Link.** The fork claims a free player number by watching the network
  (`virtual_cdj.rs`), so two decks on one network do not need different
  settings.
- **Panel.** See the skin decision in §2.

### 3.8 Reaching the deck at a venue
No home Wi-Fi there, and last-minute fixes still have to be possible.
- [x] **ssh over Ethernet already works.** sshd listens on every interface
      (`0.0.0.0:22`, `[::]:22`), eth0 included. On the CDJs' switch eth0 holds a
      169.254 link-local address; a laptop on the same switch gives itself one
      too, then `ssh sam1902@trimixxx2.local` (avahi answers on every
      interface) or the address the Diagnostics page shows.
- [x] **Diagnostics → Network** (fork, `WDeckDiagnostics`), right under
      Identity: host and the ssh line to type; eth0's live address, or "no
      cable" / "linked, no address yet"; Wi-Fi as home network + address, or
      HOTSPOT + name + password + address, from `/run/trimixxx/wifi`. Built cold
      after the Docker prune and deployed with `mixxx/upload.sh`. Seen on the
      panel: `trimixxx2  ssh sam1902@trimixxx2.local`, `Ethernet: no cable`,
      `Wi-Fi: Odildo-3F96AC · 192.168.1.118`. The eth0-address and hotspot
      lines are still to be seen with a cable / during a fallback.
- [x] **Wi-Fi fallback** (`pi_config/wifi-fallback/`, see its README): if wlan0
      has not joined a network 45 s into a boot (30 s more if mid-connection),
      the deck brings up its own access point — SSID = hostname, password
      `trimixxx-debug-Kmj3Df` (`wifi-fallback/hotspot.env`), 2.4 GHz channel
      from the deck's unit file, `hotspotChannel` (Trimixxx1 6, the default;
      trimixxx2 7), the deck at 10.42.0.1. Once per boot, never undone; a
      reboot retries home. shellcheck clean. Installed inert (`autoconnect=no`
      profile, unit enabled not started, wlan0 and the default route checked
      unchanged). Tested:
      - at home it does nothing but record `mode=client` (started by hand, then
        on a real boot): 39 ms to start, not on Mixxx's critical chain — getty
        at 16.4 s is NetworkManager's 7.8 s plus the launcher's 3.3 s gesture
        window, as before;
      - the one-off test boot (`echo 300 | sudo tee
        /var/lib/trimixxx/wifi-fallback-test`, reboot): `trimixxx2` appeared,
        a phone joined with the password, and ssh over it worked. Rebooted by
        hand mid-hold: a normal boot, flag gone, home Wi-Fi back, wlan0's
        runtime autoconnect back on.
      The test boot's own log is gone — journald is volatile on this image (no
      `/var/log/journal`), so nothing survives a reboot. Make the journal
      persistent first if a fallback ever needs post-mortem reading.
- [x] The rescue console (hold CUE at boot) prints `/run/trimixxx/wifi` too, so
      the hotspot's name and password are on screen when Mixxx is what broke.
- [x] **A USB keyboard gets a shell, two ways, no network needed.**
  - Any time: Ctrl+Alt+F2 → a password login on tty2 (logind's on-demand
    gettys, `NAutoVTs` default 6; X allows VT switching). Ctrl+Alt+F1 back to
    Mixxx. If X is hung, Alt+SysRq+R first (`kernel.sysrq` 438 allows it;
    Alt+SysRq+B reboots on the spot). `sam1902` has a password, set at flash.
  - The rescue console: a shell already logged in, no X. **It could not have
    worked before this**: it opened `bash --login`, which re-reads
    `~/.bash_profile`, which on tty1 with the mode still `debug` ran the console
    again — inside itself, without end, until sam1902 ran out of processes (and
    then even ssh as sam1902 cannot fork). Now `bash -i`, plus a
    `TRIMIXXX_RESCUE` guard in `~/.bash_profile`. Tested by writing the mode
    file (`echo debug | sudo tee /run/trimixxx/mode`, the launcher restarts the
    session) with a root timer as backstop: exactly one level (login shell →
    `trimixxx-debug` → `bash -i`), the banner and a prompt on tty1; hanging the
    shell up ran the real hand-back — mode `mixxx`, X and Mixxx back, the
    deck-keys bridge stopped. The CUE gesture itself is still to be tried by
    hand.
  - **No keyboard at all: the touchscreen.** The rescue console starts X with
    the shell in an `xterm` on the left half and `matchbox-keyboard` on the
    right (`rescue-session`, `rescue-keyboard.xml`; `xterm` +
    `matchbox-keyboard`, no recommends: 5 small packages). Focus is pinned to
    the terminal, since with no window manager X would hand it to whatever the
    finger last touched; the keyboard types through XTEST. If X does not come
    up, the bare console takes over (a marker file tells "left" from "never
    started"; seen working when the keyboard was failing). What it took:
    this build segfaults on *any* layout argument, so ours is
    `~/.matchbox/keyboard.xml`, the default name; the stock layouts have no
    digits, Tab, Ctrl or arrows; its labels need DejaVu Sans (one font, no
    fallback — boxes otherwise) and two-unit keys for words; its colours are
    compiled in, so picom (xrender) inverts the window to dark; and it keeps a
    2:1 shape, 640×320 at the top of the right half, which is fine. Typing by
    touch confirmed on the deck.
  - **Pads at half brightness in the rescue console**: `trimixxx-deckkeys`'s
    console map lit every pad at the Doom map's levels, the whole deck at
    once. Now `RGB.Scaled(0.5)` on every ring pad of that map — half the duty
    cycle, half the current — and Doom unchanged (`trimixxx-launcher`,
    `internal/keymap`). The lamps (play, cue, loop) are on/off only.

## 4. Final check, after a cold boot
- [ ] `systemctl --failed` is empty; `systemd-analyze critical-chain getty@tty1.service`
- [ ] `grep -E 'TriMixxx|pi-midi-daemon' /proc/asound/seq/clients` — both ports
- [ ] `cat /run/trimixxx/mode` → `mixxx`
- [ ] Mixxx: the UCA222 opened, both controllers loaded (`/tmp/mixxx/mixxx.log`)
- [ ] `pi-qemu deck shot --host trimixxx-pi-2 deck.png` shows the TriMixxx skin;
      `deck tap` and `deck press` (the same `--host`) move it
- [ ] A rekordbox stick mounts at `/media/DJ_USB_1` and shows in the library
- [ ] `ip -4 addr show eth0` → `169.254.x.x` with a CDJ attached; players appear
- [ ] Boot gestures: hold PLAY → Doom, hold CUE → debug console; the panic chord
- [ ] POWER menu shuts the deck down cleanly
- [ ] Temperature after half an hour of playing

## 5. State that only existed on Trimixxx1
Not in the repo, so not copied. Recreate it or accept the loss:
- **Its `config.txt`.** Probably a DSI overlay for its 1024×600 panel: the splash
  notes record a 720×576 firmware framebuffer before vc4 takes over, which is
  what the firmware falls back to with no HDMI display.
- `/var/lib/alsa/asound.state` (§3.6).
- `~/.mixxx/mixxxdb.sqlite`: library, analysis, playlists — and any tracks
  stored on its card under `~/Music`.
- Its `effects.xml` as last left at the mixer; trimixxx2 starts from the shipped
  seed.
- Anything installed or scheduled by hand. No systemd timers or cron jobs are
  versioned anywhere in the repo, so any it had lived only on that card.

## 6. Repo fixes made during this install
Found by running the deploy path against a blank unit. Uncommitted as of this
writing.
- `pi_config/upload.sh` — **did not parse** (the apostrophe in §3.2); and a
  preflight that stops before changing anything if ttymidi is not installed.
- `pi_config/prolink-eth0.sh` — no cable in eth0 sets the profile and reports
  "comes up when plugged in" instead of failing.
- `mixxx_config/upload.sh` — creates `~/.mixxx/skins` and `~/.mixxx/controllers`
  before copying into them.
- `pi_config/xinitrc` — DSI panel primary, HDMI mirrored, touch pinned to the
  panel; `QT_SCALE_FACTOR` fitted to the panel (1.25 on 1280×800, nothing on
  1024×600, §2); `mkdir -p ~/.mixxx` before the Mixxx loop, without which a
  fresh unit never starts Mixxx: the shell opens the `2>>~/.mixxx/stderr.log`
  redirect before `mixxx` and fails on the missing directory.
- Not changed, worked around: `mixxx_config/ttymidi/Makefile`'s
  `install-remote` fails on a unit without the bridge unit yet — pass
  `SERVICE=` (§3.1). The fix belongs in the ttymidi submodule. The target has
  gone since, and the ttymidi step restarts the bridge only if it runs.
- `mixxx_config/mixxx.cfg` — `ReplayGainEnabled 0` (§3.6); the reason is in
  `mixxx_config/README.md`, since Mixxx rewrites `mixxx.cfg` and drops comments.
- Per-deck wiring (§3.4): `mixxx_config/units/trimixxx2.json` and
  `units/apply.py`; `mixxx_config/upload.sh` stages the mapping through it;
  `TriMixxx.scripts.js` gains the `// @unit-wiring` table, `ringLed()` /
  `ringLedPair()` address the physical node, and the hot-cue handler reads the
  canonical note; `pi_config/deck-poke`'s named verbs send what the deck's own
  S3 would. `mixxx_config/README.md` documents it, and its encoder row now says
  push over the deck is the FX mute. Later the same evening: `jogReversed` in
  the unit file, read by `TriMixxx.jog`; `TriMixxx.RING_LEVEL` (0.8, gamma
  corrected), the rings' overall brightness; and `"bezel"` in the unit file —
  `[TriMixxx],bezel` in `skin.xml` with open maximum heights, `apply.py` and
  `upload.sh` staging the skin, and in the `mixxx/` submodule
  `src/widget/deck/deckbezel.h` plus `WDeckBrowser`/`WDeckRack` reading it.
- Reaching the deck at a venue (§3.8): `pi_config/wifi-fallback/` (script,
  unit, installer, README), hooked into `pi_config/upload.sh`;
  `pi_config/trimixxx-debug` prints the fallback's decision; in the `mixxx/`
  submodule, `WDeckDiagnostics` gains the Network section.
- The rescue console's shell (§3.8): `pi_config/trimixxx-debug` opens `bash -i`
  instead of `bash --login`, which recursed without end, and `pi_config/bash_profile`
  stands down under `TRIMIXXX_RESCUE`.
- The touch rescue console (§3.8): `pi_config/rescue-session` and
  `pi_config/rescue-keyboard.xml`, `trimixxx-debug` trying them first, and
  `pi_config/upload.sh` installing them with their two packages; in
  `trimixxx-launcher`, the console map's pads at half brightness.

Since then, `pi-qemu` has replaced the scripts named here and in §3:

| Then | Now |
|---|---|
| `pi_config/upload.sh`, with `prolink-eth0.sh`, `splash-install.sh`, `wifi-fallback/install.sh` and `dj-usb/install.sh` | the system step, `pi-qemu/deploy/002_system.sh` |
| ttymidi's and the launcher's `make install-remote` | the ttymidi and launcher steps (`001`, `003`) |
| `mixxx/upload.sh` | the mixxx step (`004`) |
| `mixxx_config/upload.sh` | the config step (`005`) |
| `doom/install.sh` | the doom step (`007`) |
| `pi_config/deck-shot`, `deck-poke`, `deck-record` | `pi-qemu deck shot`; `deck tap`, `swipe`, `press`, `browse`, `midi`; `deck record` |
