# Fresh install — from Raspberry Pi OS Lite to a working deck

The order to bring a deck Pi up from a freshly flashed card, written while doing
it for **trimixxx2** (started 2026-10-01). Trimixxx1 was away and unreachable,
so everything here comes from this repo and from the new unit itself — which is
also how the state that only ever lived on Trimixxx1 was found. It is listed at
the end.

Generic Raspberry Pi setup first, TriMixxx after. Each step records what was
done on trimixxx2 and what it showed.

`[x]` done on trimixxx2 · `[ ]` to do · `[~]` skipped or deferred, with why

> **Every script here defaults to `HOST=trimixxx-pi`** — Trimixxx1. That is the
> deploy scripts and `deck-shot` / `deck-poke` / `deck-record` alike. Prefix
> each one with `HOST=trimixxx-pi-2` (or pass `HOST=trimixxx-pi-2` to `make`),
> or it goes to the wrong deck.

## Where it stands

Hardware finished and the case closed (panel, touch and the S3 all wired).
**The deck runs**: the TriMixxx skin on the panel, the controller live over the
UART both ways, the UCA222 at full output. Done: §1 (bar the deferred EEPROM
update), §2, §3.0–3.6 (bar the UCA222's MONITOR switch, hardware). The
deploy-script fixes it took are in §6, not committed yet.

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
- [x] **Passwordless sudo.** Every deploy script runs `ssh host 'sudo …'` with no
      terminal, so a password prompt is a hard failure, and Raspberry Pi OS no
      longer ships the old `010_pi-nopasswd`. Installed
      `/etc/sudoers.d/010_sam1902-nopasswd` (`sam1902 ALL=(ALL) NOPASSWD: ALL`),
      checked with `visudo -cf` before it went in. The same line was also added
      to `/etc/sudoers` (line 48) by hand; either one alone is enough.
- [ ] *Optional:* `PasswordAuthentication no` for sshd. Only with a second
      working key in `authorized_keys` — it holds two today.

### 1.2 Firmware
- [~] `sudo rpi-eeprom-update -a`. An update is pending (bootloader 2026-01-09 →
      2026-09-23), but the deck boots fine on the current one, and a failed
      flash would need the SD card out — the case is closed. Do it the next time
      the case is open anyway.

### 1.3 Boot flags — `/boot/firmware/config.txt`, `cmdline.txt`
Originals kept as `config.txt.pre-trimixxx` and `cmdline.txt.pre-trimixxx`
next to them. Three stock lines changed in place, and one block appended at the
end of `config.txt`:

```
dtparam=audio=on          ->  dtparam=audio=off
display_auto_detect=1     ->  display_auto_detect=0
dtoverlay=vc4-kms-v3d     ->  dtoverlay=vc4-kms-v3d,noaudio

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

### 1.5 Card wear
- [x] **log2ram:** `sudo apt install log2ram` — Debian trixie packages it now
      (1.7.2), no third-party repo, no other packages pulled in. `/var/log` (576 K
      at the time) moves to a 128 M tmpfs and syncs to the card daily
      (`log2ram-daily.timer`) and at shutdown. Trimixxx1 ran it. Active after the
      reboot: `findmnt /var/log` → `log2ram tmpfs`.
- [~] Cap journald to fit in log2ram's RAM disk — not needed here: this image
      has no `/var/log/journal`, so the journal is volatile and already lives in
      `/run`, not in `/var/log`.
- Note: the biggest card writer is not `/var/log` but Mixxx's own logs in
  `~/.mixxx`. `xinitrc` runs it with `--developer --log-level debug`, measured at
  ~2 MB/s, capped at 128 MB per file. log2ram does not cover them.

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
      `startx` never wakes. `scrot` / `xdotool` are for `deck-shot` and
      `deck-poke`; `evtest` / `libinput-tools` / `mesa-utils` are for checking
      the panel.
- [x] **`xinitrc`, two fixes** (this repo, §6): the DSI panel is the primary
      output, an HDMI monitor mirrors it scaled to fit (debugging), and the
      touchscreen is pinned to the panel; and `mkdir -p ~/.mixxx` before the
      Mixxx loop. Its `awk` parsing was checked against sample `xrandr` /
      `xinput` output under the Pi's own `mawk`.
- [x] Session files onto the Pi: `~/.xinitrc`, `~/.bash_profile`,
      `/usr/local/bin/trimixxx-debug` — the session part of `upload.sh`, run on
      its own so the panel can be tested before the rest of the deck exists.
      There was no previous `~/.bash_profile`, and `~/.profile` has no `startx`.
- [x] **Console autologin on tty1:** `sudo raspi-config nonint do_boot_behaviour B2`
      — writes `getty@tty1.service.d/autologin.conf`
      (`agetty --autologin sam1902`). No script in the repo does this; Trimixxx1
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
Docker Desktop running (all Pi binaries are arm64 builds in Docker), `uv`, and
`rsvg-convert` (`brew install librsvg`) for the splash.
- [x] Built while the Pi was off, so the deploys below hit a warm cache:
      `ttymidi`, `trimixxx-launchd` and `trimixxx-deckkeys` (static arm64), and
      the Mixxx fork at HEAD `08174e6` (12 min). The `mixxx/dist/mixxx` on disk
      predated that commit. Each deploy rebuilds anyway, from that cache.

### 3.1 ttymidi — before `pi_config/upload.sh`
- [x] `make -C mixxx_config/ttymidi install-remote HOST=trimixxx-pi-2 SERVICE=`
      → `/usr/local/bin/ttymidi`, `ttymidi --version` → `42175e8`.

  First because `upload.sh` enables and *restarts* `trimixxx-bridge.service`,
  which needs this binary. `upload.sh` now checks for it and stops before
  changing anything if it is missing (§6). **Pass `SERVICE=` (empty) on a fresh
  unit:** the target ends with `systemctl try-restart trimixxx-bridge.service`,
  which its own comment calls a no-op when the unit is absent — it is not,
  systemd exits 5 for a unit that does not exist yet, and `make` reports a
  failure after the binary was in fact installed.

### 3.2 `pi_config/upload.sh`
- [x] `HOST=trimixxx-pi-2 ./upload.sh` — exit 0, end to end, the first complete
      run since 2026-08-01. Fixed before first use here (§6):
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
      1280×800, 16 bpp, stride 2560, 2 048 000 bytes, matching. Re-run
      `HOST=trimixxx-pi-2 ./splash-install.sh` if the panel or its mode changes.
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
- [x] `make -C trimixxx-launcher install-remote HOST=trimixxx-pi-2` — active, both
      readiness gates passed (`pi-midi-daemon` port, mode file), mode `mixxx`.

### 3.4 Mixxx
- [x] `apt install mixxx` (2.5.0+dfsg, trixie) — for its libraries and
      `/usr/share/mixxx`. Only the binary gets replaced.
- [x] `HOST=trimixxx-pi-2 ../mixxx/upload.sh` — builds the fork and swaps
      `/usr/bin/mixxx` (24 MB), keeping apt's as `/usr/bin/mixxx.apt` (17 MB).
      The `ldd` check found nothing missing. The running Mixxx has
      `QT_SCALE_FACTOR=1.25` in its environment, from `xinitrc`.
- [x] **Once, before the first config upload:**
      `ssh trimixxx-pi-2 'sudo systemctl stop getty@tty1 && rm -f ~/.mixxx/effects.xml && mkdir -p ~/Music'`.
      Stock Mixxx ran at the first boots and writes its own `effects.xml` *on
      exit* — so stop the session first, or it writes it straight back.
      `upload.sh` only seeds the TriMixxx chain when the deck has none, so it
      would otherwise keep the stock rack, and the effect-pedal return would be
      silent. `~/Music` is for the next point. On trimixxx2 there turned out to
      be no `effects.xml` to remove: Mixxx had never got past its first-run
      dialog, so it never wrote one. Keep the step anyway — it costs nothing.
- [x] `HOST=trimixxx-pi-2 ../mixxx_config/upload.sh` — now creates
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
      `~/Music` did not exist, which is why "Choose" was greyed out.
- [ ] Check Preferences → Controllers: **TriMixxx** and **pi-midi-daemon** both
      enabled with their mappings. `mixxx.cfg` binds them by device name, so the
      upload should already have done it.
- [x] **This deck's button wiring** (`mixxx_config/units/trimixxx2.json`). BACK
      seemed dead: pressing every control in order with `aseqdump -p TriMixxx`
      running showed ring A has an **eighth pad, third in its chain** (note
      `0x02`, no function yet), which pushes 8-beat, 4-beat, ×2, ÷2 and BACK one
      node down (`0x03`–`0x07`) — so ÷2 was opening the library and BACK sent a
      note nothing listened to — and **hot cues 1–4 arrive reversed**
      (`0x46`…`0x43`). Tempo range, keylock, slip, sort, loop in/out, reloop and
      the encoder are standard; PLAY/CUE too (the capture had them the other way
      round — taken as pressed in the other order). Fixed in Mixxx, not the S3:
      `upload.sh` renumbers the mapping from that file for this deck alone
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
      config upload.
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
      then `HOST=trimixxx-pi-2 ./upload.sh` in `mixxx_config`.

### 3.5 Doom
- [x] `HOST=trimixxx-pi-2 ../doom/install.sh` (the WAD is already in `doom/wad/`)
      — Chocolate Doom 3.1.0, 17 packages. Among them are `libpulse0` and
      `libpipewire-0.3`, the client libraries SDL2 links against; the PulseAudio
      and PipeWire *servers* are only suggested and are not installed.

### 3.6 Audio levels
- [x] **UCA222 output to 0 dB.** It came up at −20 dB (108/128). Anything under
      unity throws away 16-bit resolution:
      `amixer -c CODEC sset PCM 0dB && sudo alsactl store` → 128/128, 0.00 dB,
      saved in `/var/lib/alsa/asound.state`, which is not versioned (see
      README, "Not yet versioned here").
- [ ] **UCA222 MONITOR switch OFF** — hardware. Left on, it feeds the input
      straight back into the output (`worklog/effect-pedal`).
- [x] **DECIDED — normalization off** (`ReplayGainEnabled 0` in the shared
      `mixxx.cfg`, live on trimixxx2; Trimixxx1 gets it on its next config
      upload). Why, from the investigation:
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

## 4. Final check, after a cold boot
- [ ] `systemctl --failed` is empty; `systemd-analyze critical-chain getty@tty1.service`
- [ ] `grep -E 'TriMixxx|pi-midi-daemon' /proc/asound/seq/clients` — both ports
- [ ] `cat /run/trimixxx/mode` → `mixxx`
- [ ] Mixxx: the UCA222 opened, both controllers loaded (`~/.mixxx/mixxx.log`)
- [ ] `HOST=trimixxx-pi-2 ./deck-shot` shows the TriMixxx skin; `deck-poke` moves it
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
  `SERVICE=` (§3.1). The fix belongs in the ttymidi submodule.
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
