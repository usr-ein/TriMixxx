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
  format-clean. Requires `brew install clang-format`.
- **Lint:** `pio check` runs cppcheck over `src/` + `lib/` only (framework and
  lib-deps skipped). Suppressions for third-party headers and the canonical
  encoder state table live in `platformio.ini`. Requires `brew install cppcheck`.
- **Git hook (automatic):** both of the above run on every commit via `prek`,
  configured in the monorepo-root `.pre-commit-config.yaml` (scoped to this
  firmware). clang-format auto-fixes staged C/C++; `pio check` blocks the commit
  on any medium/high cppcheck defect. **One-time setup per clone:** `prek install`.
  Tools needed: `brew install clang-format cppcheck` + `prek`.
  (Note: `pio check --fail-on-defect` matches an *exact* severity, so the hook
  lists both `medium` and `high` — see the config comment.)

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
| `lib/TempoFader` | Ratiometric slide-fader read: center-tap (ADCT) + wiper (ADIN), both ADC1. Value derived from `ADIN - ADCT` so center maps to the midpoint with no calibration. **14-bit** output (0..16383) sent as a MIDI CC MSB/LSB pair; per-side spans for asymmetric hardware. Split-EMA smoothing + hysteresis. |
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
- Tempo: 14-bit CC pair — `CC_TEMPO` (MSB) + `CC_TEMPO_LSB` (= MSB+32); Mixxx must bind both as `<fourteen-bit-msb>`/`<lsb>`.
- Track encoder: `CC_ENCODER` relative (1=up, 127=down) + `NOTE_ENC_SW` press.
- Reserved (drivers not built yet): `NOTE_PLAY/CUE`, `NOTE_LOOP_*`.

## Status / what's stubbed
Working: **ring A pads** (MIDI in/out + LED echo), the **jog wheel**, the **tempo
fader**, and the **track encoder**. Remaining — **play/cue board** and **loop
board** — have reserved MIDI addresses and live Mixxx bindings but no driver
modules yet. The `loop()` in `main.cpp` has comment stubs showing what each sends.

## Conventions
- New peripheral = new self-contained module in `lib/<Name>/`, wired only in `main.cpp`. Keep MIDI/Mixxx knowledge out of drivers.
- Add any new MIDI address to `MidiMap.hpp` first, then the Mixxx mapping.
- `test/` is a PlatformIO test dir (currently only the boilerplate README).

## Embedded best practices
Follow these when writing/reviewing firmware here — they're the house rules and
each has a live example in the tree.

- **No dynamic allocation.** No `malloc`/`calloc`/`new`/`std::vector` on the heap.
  Size buffers statically to a compile-time cap and clamp runtime inputs to it —
  see `OneButtonRing` (`MAX_NODES`, fixed arrays, `nodeCount` clamped in the ctor).
  This is deterministic: no fragmentation, no OOM, no leak/double-free, and RAM
  use is known at link time. If you ever *think* you need the heap, cap it and use
  a static array instead; only reach for allocation if a size is genuinely
  unbounded (it isn't, for this hardware).
- **Self-size to reality, within the cap.** Prefer discovering the actual size at
  runtime (e.g. ring enumeration → `_active`) over trusting a hardcoded count, but
  keep the static buffer sized to the worst case.
- **Debounce mechanical contacts, never clean digital.** KY-040 (`TrackEncoder`)
  bounces → state-table decode. Optical jog (`JogWheel`) is comparator-clean → raw
  edge count, no debounce. Match the treatment to the signal.
- **Use hardware peripherals over CPU/ISR** when one exists: PCNT for the jog
  quadrature (zero CPU, nothing missed), UART for the rings.
- **Guard state shared between the FreeRTOS task and `loop()`** with the module's
  `portMUX` critical section (`OneButtonRing`), and mark cross-thread flags
  `volatile`. Keep critical sections and ISRs as short as possible.
- **Filter noisy analog** — oversample + EMA, and add hysteresis before emitting
  (`TempoFader`). Use **ADC1 only** (IO1–10); ADC2 is unusable with WiFi active.
- **Prefer `constexpr` over `#define`** for typed constants; keep pin numbers as
  `#define`/`constexpr` at the top of `main.cpp`.
- **Fixed loop cadence, no long blocking.** Don't add long `delay()`s to `loop()`;
  latency-sensitive work (jog, MIDI) runs every pass.
