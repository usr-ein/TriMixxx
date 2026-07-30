# TriMixxx S3 — Mixxx controller mapping

Everything that lives under `~/.mixxx` on the deck. The S3 firmware sends raw MIDI
over UART → `ttymidi` on the Pi → ALSA → Mixxx sees a standard MIDI device. These
files map that device to Mixxx controls. Addresses match the firmware's
[`lib/PiLink/MidiMap.hpp`](../firmwares/trimixxx-midi/lib/PiLink/MidiMap.hpp) exactly — **change both together.**

The system side (systemd units, udev, USB automount) is in
[`../pi_config`](../pi_config); `upload.sh` here touches only `~/.mixxx` plus the
per-user font directory `~/.local/share/fonts`, and restarts Mixxx, so a mapping
tweak can never disturb the deck's system config.

## Files
- `TriMixxx.midi.xml` — the deck mapping (inputs, LED outputs).
- `TriMixxx.scripts.js` — scripting for the jog (scratch/bend), the browse
  encoder, play toggle, the ring buttons and their RGB LED indicators, and the
  startup rainbow-wave animation.
- `PiMidiDaemon.midi.xml` / `PiMidiDaemon.scripts.js` — the mapping for
  [`../pi-midi-daemon`](../pi-midi-daemon), a second MIDI device. Turns the skin's
  POWER menu into a shutdown SysEx, and turns the daemon's USB-mount events into
  a Rekordbox device rescan.
- `TriMixxx_skin/` — single-deck CDJ-style skin for the 1024×600 touchscreen.
- `fonts/` — MesloLGL Nerd Font (Regular + Bold), installed to the deck's
  `~/.local/share/fonts` by `upload.sh`. Both `mixxx.cfg` (`[Library] Font`) and
  the skin's stylesheet name this family, and Qt resolves families through
  fontconfig *by name*, so a missing font is never an error — it just silently
  falls back and the deck comes up looking subtly wrong. Only the two weights the
  skin uses are shipped; italic is synthesised. See the note in `style.qss`
  before swapping it: **every** Meslo variant is monospace, the `Mono`/`Propo`
  suffix only spaces the icon glyphs, so no Nerd Font spelling will make titles
  narrower — that needs a different family.

  Meslo is a *terminal* font (~13k codepoints), so it has no CJK, Arabic, Hebrew,
  Indic scripts or emoji. Those are handled by the Noto fallback chain in
  [`../pi_config/60-trimixxx-fonts.conf`](../pi_config), which is also what
  installs them — the deck needs **both** uploads for a track title in Japanese
  to render.
- `soundconfig.xml` — audio device + buffer size. Mixxx keeps sound hardware in
  its own file, *not* `mixxx.cfg`. Deployed by `upload.sh`; see the buffer note
  below.
- `mixxx.cfg` — the rest of the Mixxx preferences as deployed.
- `upload.sh` — deploy all of the above to the deck. **Validates every XML before
  anything leaves this machine**: Qt rejects a malformed skin silently and boots
  the default one instead, with nothing in the log.
- `serial_midi_bridge.py` — bridge/monitor the S3's UART on a Mac (or over SSH on
  the Pi) without ttymidi. `led_test.py` — send the LED feedback Mixxx would send,
  straight down the serial line, to test the return path with Mixxx out of the way.
- `ttymidi/` — our ttymidi fork (submodule); the binary `trimixxx-bridge.service`
  runs.

## Install
Copy the mappings into Mixxx's controller mapping folder, then pick **TriMixxx S3**
under Preferences → Controllers for the MIDI device (and `pi-midi-daemon` for its
own port, if that daemon is running).
- Linux: `~/.mixxx/controllers/`
- macOS: `~/Library/Containers/org.mixxx.mixxx/Data/Library/Application Support/Mixxx/controllers/`
- Windows: `%USERPROFILE%\Mixxx\controllers\`

On the deck itself, `./upload.sh` does all of this. Requires Mixxx **2.4+** (the
deck runs [our 2.5.6 fork](../mixxx)). One deck = `[Channel1]`.

## Control map
| Control | MIDI | Mixxx |
|---|---|---|
| Play | note `0x3C` | `play` (toggle); LED ← `play_indicator` |
| Cue | note `0x3D` | `cue_default` (momentary); LED ← `cue_indicator` |
| Loop in / out | notes `0x3E` / `0x3F` | `loop_in` / `loop_out`; both LEDs ← `loop_enabled` |
| Reloop | note `0x40` | `reloop_toggle` (no LED) |
| Track encoder | CC `0x10` + note `0x41` | browse library; push = open / `GoToItem` / `LoadSelectedTrack` (see below) |
| Jog | CC `0x11` + note `0x42` | scratch when touched, pitch bend otherwise |
| Tempo | CC `0x12`/`0x32` (14-bit) | `rate` |

### Ring A (notes `0x00`…, 7 buttons populated)
| # | Note | Mixxx |
|---|---|---|
| A1 | `0x00` | `TriMixxx.tempoRange` — cycle ±6 / ±10 / ±16 / Wide; LED colour keyed to the range |
| A2 | `0x01` | `TriMixxx.keylock` — master tempo |
| A3 | `0x02` | `beatloop_8_toggle` |
| A4 | `0x03` | `beatloop_4_toggle` |
| A5 | `0x04` | `loop_double` |
| A6 | `0x05` | `loop_halve` |
| A7 | `0x06` | `TriMixxx.back` — library view / focus |

### Ring B (notes `0x43`…, 6 buttons populated)
| # | Note | Mixxx |
|---|---|---|
| B1–B4 | `0x43`–`0x46` | `hotcue_1..4_activate`, on both edges (release ends the preview when paused) |
| B5 | `0x47` | `TriMixxx.slip` — flashes while slip is on |
| B6 | `0x48` | key sync — **output-only indicator**, no input mapping yet (to be driven over the CDJ LAN link) |

Both pad ranges reserve 50 notes in `MidiMap.hpp` even though fewer nodes are
populated, so adding a board never renumbers anything. `TriMixxx.RING_A_N` /
`RING_B_N` at the top of the script are the counts actually wired today.

## The screen has no buttons except POWER
Everything the skin used to put under the waveform — LIBRARY/DECK, the ±6/±10/±16/
WIDE tempo-range pads, LOOP ÷2 / ×2 — is on the hardware (ring A1, A5, A6 and the
encoder push), so the bar is gone and the waveform takes its 130 px. The jog TOUCH
indicator went with it; it was a bring-up aid for "did note `0x42` reach Mixxx",
and the waveform answers that now. POWER moved into the header, because it is the
**only** way to shut the deck down from the UI — there is no hardware equivalent.

### Library menu
Menu order is `Library::Library()` in [our fork](../mixxx/src/library/library.cpp),
not the skin: `SidebarModel` renders features in `addFeature()` order. Rekordbox,
Tracks and Players lead; everything else follows. iTunes and Serato are switched
off in `mixxx.cfg` (`ShowITunesLibrary` / `ShowSeratoLibrary`), which needs no
rebuild. Startup selection is pinned to Tracks so a boot doesn't open on
Rekordbox's "plug in a prepared device" page.

Encoder push in the left pane sends `[Library],GoToItem`, which expands an entry
that has children (Rekordbox → its USBs, Players → the CDJs on the network) and
keeps focus there, and only jumps to the track list on a leaf or on Tracks. The
bottom 56 px of the library view is a dead strip: the panel's last row sits behind
the bezel, so the last menu entry and last track row were untappable without it.

### Ring LEDs are SysEx, not `<output>`
A Mixxx `<output>` can only emit a 3-byte message, and a Note-On velocity sets
white brightness only. Colour therefore goes out as SysEx from the script —
`F0 7D <cmd> <node> <colour nibbles> F7`, `0x01` for ring A and `0x03` for ring B,
each 8-bit channel split into two data bytes so a 7-bit payload still carries
0..255. `TriMixxx.ringLed()` is the one place that formats it; `TriMixxx.led()` /
`dim()` normalise the palette so entries only need correct hue *ratios*.

## Decisions you may want to change
- **Ring button assignments** are just the tables above — repurpose freely
  (hotcues, beatjump, …); the pads are physically identical.
- **Tempo direction:** the fader is inverted in firmware; if pitch runs the wrong
  way, flip `[Channel1] rate_dir` in Mixxx (or `invert` in the firmware ctor).
- **Jog feel:** `JOG_TICKS_REV` (12960) mirrors `JogWheel::TICKS_PER_REV`;
  `JOG_BEND_SENSITIVITY` (0.3) damps pitch bend only — scratch stays 1:1. The
  scratch `alpha`/`beta`/`rpm` in `jogTouch()` are the usual starting values.
- **Audio buffer — `latency="3"` in `soundconfig.xml` can cause crackling.**
  It's set low on purpose, for responsiveness. **If crackling is really a
  problem, put it back to `4`** — that's the known-good value and the only
  change needed.

  `latency` is an index, not milliseconds: `frames = bit_ceil(samplerate_khz)
  << (latency-1)`, so at 44.1 kHz `3` = 256 frames = 5.8 ms and `4` = 512 =
  11.6 ms. It's worth more than plain output latency because Mixxx filters the
  `jog` pitch-bend control through a 25-tap moving average whose taps are
  *buffers*, not ms (`ratecontrol.cpp`) — so bend smear is 25 × buffer: ~145 ms
  at `3` vs ~290 ms at `4`. **Scratch does not go through that filter** and
  barely notices the buffer, so this only buys bend responsiveness.

  The trade is real and measured: `3` produced 12 genuine DAC underruns
  (`underflowHappened code: 6`) during 13 s of scratching, where `4` produced
  none under identical conditions. Scratching is the load that breaks it — idle
  is always clean. Two gotchas when checking this yourself: Mixxx only logs
  underruns when launched with `--developer` (`~/.xinitrc`), and that flag locks
  inside the audio callback so it can *cause* dropouts — take it back off
  afterwards. The skin's XRUN readout won't settle it either: it counts every
  underflow source, including the harmless network device (codes 24/25), and it
  read "low" at `3` while PortAudio was dropping buffers. Only code `6` means
  the DAC.
