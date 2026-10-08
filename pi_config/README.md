# pi_config — deck Pi system configuration

System-level config for the deck's Raspberry Pi: the systemd units, udev rules,
and CPU tuning that need `sudo` to install. This is the counterpart to
[`../mixxx_config`](../mixxx_config), which is purely user-space Mixxx config
under `~/.mixxx`.

The split is the point: the system deploy step installs what is here, and the
config step installs `mixxx_config` into `~/.mixxx` and restarts Mixxx. So a
routine mapping tweak can never disturb the deck's system config, and a system
change here never has to go through the config step.

## Files
- `fresh-install.md` — **start here for a new unit.** Everything between a freshly
  flashed Raspberry Pi OS Lite card and a working deck, in order: the boot flags,
  sudo, autologin and packages (the base deploy step does all but sudo), then
  the deploy steps in the order they run. Written while bringing up trimixxx2.
- `cpu-governor.service` — pins all cores to the `performance` cpufreq governor.
  `ondemand` polls load every 100 ms and only ramps past 50 % of *total* CPU, so
  a single saturated audio core can sit at the 600 MHz floor and starve a scratch
  — the measured cause of xruns at small buffers. See the unit's own comment.
- `trimixxx-bridge.service` — the `ttymidi` serial↔MIDI bridge. Gates
  `getty@tty1` (hence Mixxx) at boot so the deck's virtual MIDI port exists
  before Mixxx enumerates devices. The `ttymidi` binary itself is built from the
  submodule in `../mixxx_config/ttymidi`, by the ttymidi deploy step.
- `trimixxx-lights-off` + `trimixxx-lights-off.service` — every button light off
  at shutdown. The S3 stays powered after the Pi halts, so whatever Mixxx last
  lit (the PLAY lamp of a track that was playing) used to stay lit on a deck
  that was off. The unit does nothing at boot; *stopping* it is what runs the
  script, and its ordering puts that after Mixxx and the launch manager are
  gone but before ttymidi is: Note-offs for the four lamps, and a black ring-LED
  SysEx for all 50 nodes of each ring (the S3 ignores nodes it doesn't have), 25
  at a time — ttymidi's ALSA queue holds 200 cells and a SysEx takes two. Try it
  with `sudo systemctl stop trimixxx-lights-off` (then `start` to re-arm).
- `trimixxx-swap-sizes.conf` — boot speed: pins rpi-swap's sizes so its
  generator stops running Perl on every boot. Its companion, also in the
  system step, puts the DSI panel's drivers in the initramfs
  (`/etc/initramfs-tools/modules`) so the panel comes up at ~1.7 s instead of
  ~6 s. The `config.txt` and EEPROM halves of the same work, and why the
  initramfs itself stays, are in `fresh-install.md` §1.4.
- `99-prolink-ports.conf` — `/etc/sysctl.d` drop-in lowering
  `net.ipv4.ip_unprivileged_port_start` to 111, so Mixxx can bind the RPC
  portmapper without root and serve its rekordbox USBs to real CDJs. A CDJ asks
  the portmapper for the mountd/nfsd ports *before* it will list us as a source
  at all, and retries forever if nothing answers — so without this, serving
  fails in a way that looks like a discovery bug
  (`../prolinks-compat/docs/FINDINGS.md` F46). Not `setcap`, because the
  mixxx deploy step swaps the `/usr/bin/mixxx` binary and would drop file
  capabilities on every deploy. See the file's own comment for the trade-off.
- `60-trimixxx-fonts.conf` — `/etc/fonts/conf.d` rule giving the deck's UI font a
  fallback chain, plus the `fonts-noto-*` packages it points at (installed by
  the system step). The UI font itself, MesloLGL Nerd Font, ships from
  [`../mixxx_config/fonts`](../mixxx_config) — it is a *terminal* font, ~13k
  codepoints, so a track title in Japanese, Korean, Arabic, Hebrew, an Indic
  script, or with emoji in it, has no glyphs and renders as tofu boxes. Mixxx can
  only be told one family name, so per-character fallback is fontconfig's job and
  this is where the order is pinned. `fonts-noto-extra` (Tibetan, Yi, the rarer
  scripts) is deliberately left out — a few hundred MB; add it if you want them.
- `getty-tty1-stop-mixxx.conf` — `getty@tty1` drop-in that asks Mixxx to quit and
  waits for it *before* systemd tears the session down. Mixxx handles the
  termination signal itself, but X is in the same scope and dies in the same
  instant, so without this the shutdown aborts on `The X11 connection broke`
  with settings unwritten and threads unjoined. Also asks a running Doom to quit,
  since a mode switch restarts this same unit.
- `bash_profile` — installed as `~/.bash_profile`: what the autologin session
  does. It reads `/run/trimixxx/mode` (written by
  [`../trimixxx-launcher`](../trimixxx-launcher)) and picks between a text
  console on the bare tty and `startx`; `xinitrc` then picks Mixxx or Doom
  *inside* X. The debug console skipping X is the point of it — it is the screen
  you want when the graphical stack is what broke. **No mode file, an unreadable
  one, or an unknown mode all fall through to Mixxx**, so a launch manager that
  failed to start cannot stop the deck from being a deck. Note that bash reads
  `~/.bash_profile` *instead of* `~/.profile`, so the system step backs up
  whatever was there and warns if the old `startx` line is still in `.profile`.
- `trimixxx-debug` + `rescue-session` + `rescue-keyboard.xml` — the rescue
  console: hold CUE from power-on. On the touchscreen, with nothing plugged in:
  a terminal on the left half of the panel and an on-screen keyboard on the
  right (`matchbox-keyboard`, dark, with a layout made for a shell — Esc, Tab,
  Ctrl, Alt, the arrows, every ASCII character), the shell already logged in.
  If X will not come up, the same shell on the bare console instead, which is
  the screen still there when graphics are what broke. It shows the
  interfaces, the route and the Wi-Fi fallback's decision first; `exit` hands
  the deck back to Mixxx. Meanwhile the deck's pads type cursor keys and the
  like (`trimixxx-deckkeys --map console`) and are lit at half brightness. A
  USB keyboard works too, and with one there is a way in that needs no
  gesture: Ctrl+Alt+F2 at any time for a password login on tty2 (Ctrl+Alt+F1
  back to Mixxx; if X is hung, Alt+SysRq+R first). Quirks worth knowing, all
  in `rescue-session`: this `matchbox-keyboard` segfaults when given a layout
  by name, so ours is installed as its default (`~/.matchbox/keyboard.xml`);
  it draws labels in one font with no fallback, hence DejaVu Sans; its colours
  are compiled in, so picom inverts its window; and it keeps its own 2:1 shape,
  640×320 at the top of the right half. The shell is interactive, *not* a
  login shell, and `~/.bash_profile` stands down while `TRIMIXXX_RESCUE` is set:
  a login shell there re-ran the console inside itself without end.
- `trimixxx-splash.service` + `trimixxx-splash.sh` + `splash-render.py` — the
  boot splash: `trimixxx_logo_crt.svg` on the panel for the first ~8 s of boot,
  then the boot log as normal. The usual way to do
  this is plymouth with `quiet splash`, which hides the console — but watching
  the deck's units come up is how you spot the MIDI bridge or a USB mount
  failing before a gig, so instead the splash takes an unused VT (7) and the
  console keeps printing to tty1 in the background. Switching back redraws it:
  the log is deferred, not suppressed. The Pi has no image tooling at all —
  `splash-render.py` rasterises the SVG here and packs it into the panel's exact
  framebuffer layout (read live off `/sys/class/graphics/fb0`, currently
  1024×600 RGB565), so displaying it on the deck is one `cat` to `/dev/fb0`.
  The system step runs `splash-render.py`, then installs the image, the script
  and the unit.
- `eth0-link-local.nmconnection` — eth0 for the CDJs' network. A Pro DJ Link
  network has no DHCP: CDJs take 169.254.0.0/16 addresses and broadcast to
  169.254.255.255, which a host with no address there drops, so Mixxx would
  hear no players. This NetworkManager profile, bound to eth0, is IPv4
  link-local, as the CDJs do. The system step installs it and deletes
  netplan's old eth0 profile where that sits alone in its file; every release
  card carries the same file. eth0 then gets no address on an ordinary LAN:
  wlan0 is the deck's way to the world.
- `dj-usb/` — USB stick auto-mount (udev rule → templated systemd service +
  mount helper). Installed by the system step.
- `wifi-fallback/` — the deck's own hotspot, named after its hostname, when no
  Wi-Fi has been joined 45 s into a boot: the way in at a venue. Once per boot,
  never undone; a reboot retries home. Inert until the next boot. The system
  step installs it, on the deck's own channel (`hotspotChannel` in its unit
  file) and with the password in `wifi-fallback/hotspot.env`. See its README,
  including how to try it at home with a one-off test boot.

## Deploy
```sh
pi-qemu deck deploy --host trimixxx-pi system   # a real deck, by its ssh alias
pi-qemu deck deploy NAME system                 # an emulated deck
```

The system step is `../pi-qemu/deploy/002_system.sh`. It installs everything
above (idempotent; `sudo` on the deck), and the session restarts once the steps
are over. There is no default deck: name it. The step needs
`/usr/local/bin/ttymidi` on the deck first, from the ttymidi step, which runs
before it when `pi-qemu deck deploy --host ALIAS` runs every step. The pieces
have no installers of their own: the system step installs them all.

The splash is the one piece with something to *look* at, so it has its own
try-it path (needs `uv` and `rsvg-convert`, i.e. `brew install librsvg`). From
the repo's root:

```sh
# render it here, as a PNG; no Pi involved
uv run pi_config/splash-render.py pi_config/trimixxx_logo_crt.svg -o /tmp/s.raw --preview /tmp/s.png
# show it on the deck for 5 s
pi-qemu deck ssh --host trimixxx-pi sudo /usr/local/bin/trimixxx-splash 5
```

The second runs the real boot-time script, on the image the system step
installed, so it exercises the VT switch and the blit exactly as boot does.
Mixxx is not restarted or disturbed: Xorg is on tty1 and simply loses the
foreground while the logo is up, then redraws. To change how long the splash
holds at boot, edit `SPLASH_HOLD` in `trimixxx-splash.service`.

## Looking at the deck, and touching it
From your desk, with `pi-qemu deck`. None of it is installed on the Pi: on a
real deck it runs over ssh. `pi-qemu deck shot --host trimixxx-pi out.png`
grabs the screen with `scrot`, and `deck tap`, `longpress`, `swipe`, `flick`
and `key` send touches and keys with `xdotool` (the base step installs both).
`deck press` (push, back, sort, play, cue, ...), `deck browse` (the encoder)
and `deck midi` send the deck's *own* controls as MIDI, on its wiring, so they
run the real `TriMixxx.midi.xml` and `TriMixxx.scripts.js` as if the S3 had sent
them. `deck record` records the main mix. An emulated deck takes the same verbs
with its NAME. `pi-qemu deck VERB --help` gives each one's arguments.

The MIDI injection on a real deck is not obvious
(`../pi-qemu/app/src/decks/remotedeck.cpp`): Mixxx's ALSA port has `WRITE` but
not `SUBS_WRITE`, so `aconnect` into it fails with "Operation not permitted"
while `aseqsend -p <client:port>` addressed straight at it works. The port is
resolved from the sequencer graph on every call, by following ttymidi's own
output to whoever is listening. Matching on Mixxx's pid does *not* work,
because ALSA records the thread that created the port rather than the process.

## Not yet versioned here
Some deck state still lives only on the Pi and would be lost on a re-image:
- `/var/lib/alsa/asound.state` — the UCA222 output level (set to 0 dB / unity;
  `alsactl store` persists it). Anything below unity throws away 16-bit
  resolution, so this matters.

Worth pulling into this folder if full reproducibility is wanted.

Deliberately *not* versioned: `~/.mixxx/trimixxx-levels`, the output trim and
panel brightness set on the deck itself from Diagnostics → Adjust. It is per
deck and per venue, outside `mixxx.cfg` so the config step cannot
reset it, and losing it on a re-image only puts the output back to unity and
the panel to whatever `systemd-backlight` restores.
