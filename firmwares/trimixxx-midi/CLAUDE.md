# TriMixxx S3 master (`trimixxx-midi`)

Firmware for the **LOLIN S3 Mini (ESP32-S3)** master board of a custom DJ deck. It
reads the deck's controls, bridges them to Mixxx on a Raspberry Pi as MIDI, and
drives the ring's RGB LEDs from Mixxx feedback.

## Commands

```bash
pio run                      # build
pio run -t upload            # build + flash (native USB CDC)
pio device monitor           # serial console @ 115200
pio run -t clean
pio check                    # cppcheck static analysis (config in platformio.ini)
```

Board target: `lolin_s3_mini` (single env). Native USB is used for both flashing
and the serial console — no external programmer.

### Formatting & linting
- **Format:** `clang-format` (config `.clang-format`). Apply to everything:
  `clang-format -i src/**/*.cpp lib/**/*.{hpp,cpp}`. The whole tree is kept
  format-clean — run it before committing. Requires `brew install clang-format`.
- **Lint:** `pio check` runs cppcheck over `src/` + `lib/` only (framework and
  lib-deps skipped). Suppressions for third-party headers and the canonical
  encoder state table live in `platformio.ini`. Requires `brew install cppcheck`.

## Architecture

`src/main.cpp` is the whole application: it wires the driver modules together in
`setup()` and pumps them in `loop()`. Each peripheral is a self-contained module
under `lib/` (auto-detected by PlatformIO's LDF), and each knows nothing about the
others — `main.cpp` is the only place that maps one to another.

| Module | Role |
|--------|------|
| `lib/OneButtonRing` | Master driver for one UART ring of RGB+button nodes. Owns a **FreeRTOS task** (pinned to core 0) that circulates DATA frames; exposes a thread-safe snapshot API (`level`/`pressed`/`setLed`). One instance per ring. Implements OneButton Protocol v1.0.0. |
| `lib/PiLink` | Raw MIDI-over-UART link to the Pi. Send helpers (`noteOn/noteOff/cc`) + `poll()` parses incoming MIDI (with running-status) and fires a callback. Deck-agnostic. |
| `lib/JogWheel` | Quadrature jog decode via hardware **PCNT** (zero CPU, no ISR) + active-low touch sense. Uses `ESP32Encoder`. |
| `lib/TempoFader` | Ratiometric slide-fader read: center-tap (ADCT) + wiper (ADIN), both ADC1. Value derived from `ADIN - ADCT` so center always maps to MIDI 64 with no calibration. Oversampled + EMA. |
| `lib/TrackEncoder` | KY-040 **mechanical** rotary encoder (CLK/DT/SW). Full-step Buxton state-table decoder rejects contact bounce (one tick per detent); debounced push switch. |
| `lib/PiLink/MidiMap.hpp` | `namespace midimap` — the single source of truth for every MIDI address. |

### The Pi bridge
S3 sends raw MIDI bytes over UART0 → `ttymidi` on the Pi injects them into ALSA →
Mixxx sees a standard MIDI device. **Mixxx owns all LED state**: the firmware only
echoes incoming Note-On velocity to ring LEDs (see `onMidiFromMixxx` in `main.cpp`).

### UART / voltage allocation (critical)
| UART | Peer | Voltage | Level shifter? |
|------|------|---------|----------------|
| UART0 `Serial0` (IO43 TX / IO44 RX) | Raspberry Pi (MIDI @115200) | 3.3V both ends | **No** |
| UART1 `Serial1` (IO17 TX / IO15 RX) | OneButton ring A @1 Mbaud | 5V ring | **Yes** |
| UART2 (IO13 TX / IO12 RX) | OneButton ring B (v2, `begin()` commented out) | 5V ring | **Yes** |

Jog wheel: PCNT on IO6/IO7 (A/B), touch on IO14. The optical (GP1A038RBK OPIC)
encoder is comparator-clean → raw edge-counted, **no debounce**. The KY-040 track
encoder (IO33 CLK / IO37 DT / IO38 SW) is the opposite — mechanical, so its module
debounces via a state-table decoder. Tempo fader: ADC1 on IO8 (ADCT center tap) /
IO9 (ADIN wiper), bare 3.3V pot, no external filtering.

## MIDI contract (`MidiMap.hpp`)
One deck = MIDI channel 1 (0-based `0`). `MidiMap.hpp` is the authority; the Mixxx
controller mapping (`.xml`/`.js`, **not yet created**) must be built to match it
exactly — change both together, no address overlaps.
- Ring pads: node `i` ↔ note `PAD_BASE + i` (0..49), velocity = white LED brightness.
- Jog: `CC_JOG` relative 7-bit two's-complement ticks; `NOTE_JOG_TOUCH` = scratch enable.
- Reserved (drivers not built yet): `NOTE_PLAY/CUE`, `NOTE_LOOP_*`, `NOTE_ENC_SW`+`CC_ENCODER`, `CC_TEMPO`.

## Status / what's stubbed
Working: **ring A pads** (MIDI in/out + LED echo), the **jog wheel**, the **tempo
fader**, and the **track encoder**. Remaining — **play/cue board** and **loop
board** — have reserved MIDI addresses and live Mixxx bindings but no driver
modules yet. The `loop()` in `main.cpp` has comment stubs showing what each sends.

## Conventions
- New peripheral = new self-contained module in `lib/<Name>/`, wired only in `main.cpp`. Keep MIDI/Mixxx knowledge out of drivers.
- Add any new MIDI address to `MidiMap.hpp` first, then the Mixxx mapping.
- `test/` is a PlatformIO test dir (currently only the boilerplate README).
