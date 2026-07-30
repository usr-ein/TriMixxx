# pi_config — deck Pi system configuration

System-level config for the deck's Raspberry Pi: the systemd units, udev rules,
and CPU tuning that need `sudo` to install. This is the counterpart to
[`../mixxx_config`](../mixxx_config), which is purely user-space Mixxx config
under `~/.mixxx`.

The split is the point: `mixxx_config/upload.sh` touches only `~/.mixxx` and
restarts Mixxx, so a routine mapping tweak can never disturb the deck's system
config — and a system change here never has to go through the Mixxx-restart path.

## Files
- `upload.sh` — installs everything below onto the Pi (idempotent; `sudo` on the
  far side). Override the host with `HOST=other ./upload.sh`.
- `cpu-governor.service` — pins all cores to the `performance` cpufreq governor.
  `ondemand` polls load every 100 ms and only ramps past 50 % of *total* CPU, so
  a single saturated audio core can sit at the 600 MHz floor and starve a scratch
  — the measured cause of xruns at small buffers. See the unit's own comment.
- `trimixxx-bridge.service` — the `ttymidi` serial↔MIDI bridge. Gates
  `getty@tty1` (hence Mixxx) at boot so the deck's virtual MIDI port exists
  before Mixxx enumerates devices. The `ttymidi` binary itself is built from the
  submodule in `../mixxx_config/ttymidi`.
- `99-prolink-ports.conf` — `/etc/sysctl.d` drop-in lowering
  `net.ipv4.ip_unprivileged_port_start` to 111, so Mixxx can bind the RPC
  portmapper without root and serve its rekordbox USBs to real CDJs. A CDJ asks
  the portmapper for the mountd/nfsd ports *before* it will list us as a source
  at all, and retries forever if nothing answers — so without this, serving
  fails in a way that looks like a discovery bug
  (`../prolinks-compat/docs/FINDINGS.md` F46). Not `setcap`, because
  `../mixxx/upload.sh` swaps the `/usr/bin/mixxx` binary and would drop file
  capabilities on every deploy. See the file's own comment for the trade-off.
- `60-trimixxx-fonts.conf` — `/etc/fonts/conf.d` rule giving the deck's UI font a
  fallback chain, plus the `fonts-noto-*` packages it points at (installed by
  `upload.sh`). The UI font itself, MesloLGL Nerd Font, ships from
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
  with settings unwritten and threads unjoined.
- `trimixxx-splash.service` + `trimixxx-splash.sh` + `splash-render.py` +
  `splash-install.sh` — the boot splash: `trimixxx_logo_crt.svg` on the panel
  for the first ~8 s of boot, then the boot log as normal. The usual way to do
  this is plymouth with `quiet splash`, which hides the console — but watching
  the deck's units come up is how you spot the MIDI bridge or a USB mount
  failing before a gig, so instead the splash takes an unused VT (7) and the
  console keeps printing to tty1 in the background. Switching back redraws it:
  the log is deferred, not suppressed. The Pi has no image tooling at all —
  `splash-render.py` rasterises the SVG here and packs it into the panel's exact
  framebuffer layout (read live off `/sys/class/graphics/fb0`, currently
  1024×600 RGB565), so displaying it on the deck is one `cat` to `/dev/fb0`.
  Self-contained; `upload.sh` delegates to `splash-install.sh`.
- `dj-usb/` — USB stick auto-mount (udev rule → templated systemd service +
  mount helper). Self-contained; `upload.sh` delegates to its `install.sh`.

## Deploy
```sh
./upload.sh            # all system units, to trimixxx-pi
HOST=other ./upload.sh # a different host
```

Each piece can also be installed on its own — e.g. `dj-usb/install.sh` — but
`upload.sh` is the one-shot entry point.

The splash is the one piece with something to *look* at, so it has its own
try-it path (needs `uv` and `rsvg-convert`, i.e. `brew install librsvg`):

```sh
./splash-install.sh --preview   # render and open the image here; no Pi involved
./splash-install.sh --test      # install, then show it on the deck for 5 s
```

`--test` runs the real boot-time script, so it exercises the VT switch and the
blit exactly as boot does. Mixxx is not restarted or disturbed — Xorg is on tty1
and simply loses the foreground while the logo is up, then redraws. To change
how long the splash holds at boot, edit `SPLASH_HOLD` in
`trimixxx-splash.service`.

## Not yet versioned here
Some deck state still lives only on the Pi and would be lost on a re-image:
- `/var/lib/alsa/asound.state` — the UCA222 output level (set to 0 dB / unity;
  `alsactl store` persists it). Anything below unity throws away 16-bit
  resolution, so this matters.

Worth pulling into this folder if full reproducibility is wanted.
