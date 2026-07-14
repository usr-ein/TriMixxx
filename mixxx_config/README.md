# TriMixxx S3 — Mixxx controller mapping

Mixxx mapping for the TriMixxx deck. The S3 firmware sends raw MIDI over UART →
`ttymidi` on the Pi → ALSA → Mixxx sees a standard MIDI device. These files map
that device to Mixxx controls. Addresses match the firmware's
[`lib/PiLink/MidiMap.hpp`](../firmwares/trimixxx-midi/lib/PiLink/MidiMap.hpp) exactly — **change both together.**

## Files
- `TriMixxx.midi.xml` — the mapping (inputs, LED outputs).
- `TriMixxx.scripts.js` — scripting for the jog (scratch/bend), the browse
  encoder, play toggle, and the ring position indicator.

## Install
Copy both files into Mixxx's controller mapping folder, then pick **TriMixxx S3**
under Preferences → Controllers for the MIDI device.
- Linux: `~/.mixxx/controllers/`
- macOS: `~/Library/Containers/org.mixxx.mixxx/Data/Library/Application Support/Mixxx/controllers/`
- Windows: `%USERPROFILE%\Mixxx\controllers\`

Requires Mixxx **2.4+**. One deck = `[Channel1]`.

## Control map
| Control | MIDI | Mixxx |
|---|---|---|
| Ring pads (0..49) | note `0x00`..`0x31` | **LED = play-position indicator** (script). Presses unmapped by default (see below). |
| Play | note `0x3C` | `play` (toggle); LED ← `play_indicator` |
| Cue | note `0x3D` | `cue_default` (momentary); LED ← `cue_indicator` |
| Loop in / out | notes `0x3E` / `0x3F` | `loop_in` / `loop_out`; both LEDs ← `loop_enabled` |
| Reloop | note `0x40` | `reloop_toggle` (no LED) |
| Track encoder | CC `0x10` + note `0x41` | browse library + `LoadSelectedTrack` |
| Jog | CC `0x11` + note `0x42` | scratch when touched, pitch bend otherwise |
| Tempo | CC `0x12`/`0x32` (14-bit) | `rate` |

## Decisions you may want to change
- **Ring pads are a position indicator, presses do nothing.** `MidiMap.hpp`
  defines the pads mainly as LEDs (velocity = brightness). To make pad *i*
  needle-drop to its position, uncomment/add the 50 note entries shown in the XML
  (they call `TriMixxx.padPress`). Or repurpose the pads (hotcues, beatjump, …).
- **Tempo direction:** the fader is inverted in firmware; if pitch runs the wrong
  way, flip `[Channel1] rate_dir` in Mixxx (or `invert` in the firmware ctor).
- **Jog feel:** `JOG_TICKS_REV` (12960) mirrors `JogWheel::TICKS_PER_REV`; the
  scratch `alpha`/`beta`/`rpm` in `jogTouch()` are the usual starting values.
