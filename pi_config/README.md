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
- `dj-usb/` — USB stick auto-mount (udev rule → templated systemd service +
  mount helper). Self-contained; `upload.sh` delegates to its `install.sh`.

## Deploy
```sh
./upload.sh            # all system units, to trimixxx-pi
HOST=other ./upload.sh # a different host
```

Each piece can also be installed on its own — e.g. `dj-usb/install.sh` — but
`upload.sh` is the one-shot entry point.

## Not yet versioned here
Some deck state still lives only on the Pi and would be lost on a re-image:
- `/var/lib/alsa/asound.state` — the UCA222 output level (set to 0 dB / unity;
  `alsactl store` persists it). Anything below unity throws away 16-bit
  resolution, so this matters.

Worth pulling into this folder if full reproducibility is wanted.
