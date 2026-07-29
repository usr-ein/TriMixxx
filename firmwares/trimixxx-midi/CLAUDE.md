# TriMixxx S3 master (`trimixxx-midi`)

Firmware for the **LOLIN S3 Mini (ESP32-S3)** master board of a custom DJ deck. It
reads the deck's controls, bridges them to Mixxx on a Raspberry Pi as MIDI, and
drives the ring's RGB LEDs from Mixxx feedback.

## Setup

Formatting (`clang-format`) and linting (`pio check`) run on every commit via
`prek`, configured in the monorepo-root `.pre-commit-config.yaml`. The whole tree
is kept format-clean.

**One-time setup per clone:** `prek install`.
Tools needed: `brew install clang-format cppcheck` + `prek`.

## Architecture

`src/main.cpp` is the whole application: it wires the driver modules together in
`setup()` and pumps them in `loop()`. Each peripheral is a self-contained module
under `lib/` (auto-detected by PlatformIO's LDF), and each knows nothing about the
others — `main.cpp` is the only place that maps one to another.

| Module | Role |
|--------|------|
| `lib/OneButtonRing` | Master driver for one UART ring of RGB+button nodes. Owns a **FreeRTOS task** (pinned to core 0) that circulates DATA frames; exposes a thread-safe snapshot API (`level`/`pressed`/`setLed`). One instance per ring. Implements OneButton Protocol v1.0.0. |
| `lib/PiLink` | Raw MIDI-over-UART link to the Pi. Send helpers (`noteOn/noteOff/cc`) + `poll()` parses incoming MIDI — channel-voice (with running status), **SysEx** reassembled into a fixed 64-byte buffer, and **System Real-Time ignored without disturbing the message in progress** — firing `onMidi`/`onSysEx`. Deck-agnostic. |
| `lib/JogWheel` | Quadrature jog decode via hardware **PCNT** (zero CPU, no ISR) + active-low touch sense. Uses `ESP32Encoder`. |
| `lib/TempoFader` | Ratiometric slide-fader read: center-tap (ADCT) + wiper (ADIN), both ADC1. Value derived from `ADIN - ADCT` so center maps to the midpoint with no calibration. **14-bit** output (0..16383) sent as a MIDI CC MSB/LSB pair; per-side spans for asymmetric hardware. Split-EMA smoothing + hysteresis. |
| `lib/TrackEncoder` | KY-040 **mechanical** rotary encoder (CLK/DT/SW). Full-step Buxton state-table decoder rejects contact bounce (one tick per detent); debounced push switch. |
| `lib/PlayCueBoard` | Play/cue board — 2 direct-GPIO buttons + 2 LEDs, **pins baked in** (fixed PCB). Debounced + **latched (STICKY-style) by a periodic ~2ms poll task** (`buttonPollTask`, pinned to core 1 off the ring's core 0), so a press is never missed even if `loop()` stalls; API mirrors the ring (`level`/`pressed`/`setLed`). LEDs MOSFET-driven (active-high). |
| `lib/LoopBoard` | Loop board — 3 buttons (start/end/reloop) + 2 LEDs, **pins baked in**. Same debounced poll+latch `level`/`pressed`/`setLed` API. LEDs sink-driven (active-low); reloop has a button but no LED. |
| `lib/PiLink/MidiMap.hpp` | `namespace midimap` — the single source of truth for every MIDI address. |

### The Pi bridge
S3 sends raw MIDI bytes over UART0 → `ttymidi` on the Pi injects them into ALSA →
Mixxx sees a standard MIDI device. **Mixxx owns all LED state**: the firmware only
echoes incoming Note-On velocity to ring LEDs (see `onMidiFromMixxx` in `main.cpp`).

### UART / voltage allocation (critical)
| UART | Peer | Voltage | Level shifter? |
|------|------|---------|----------------|
| UART0 `Serial0` (IO43 TX / IO44 RX) | Raspberry Pi (MIDI @115200) | 3.3V both ends | **No** |
| UART1 `Serial1` (IO17 TX / IO15 RX) | OneButton ring A @500 kbaud | 5V ring | **Yes** |
| UART2 (IO13 TX / IO12 RX) | reserved for a 2nd OneButton ring (not built) | 5V ring | **Yes** |

Jog wheel: PCNT on IO6/IO7 (A/B), touch on IO14. The optical (GP1A038RBK OPIC)
encoder is comparator-clean → raw edge-counted, **no debounce**. The KY-040 track
encoder (IO33 CLK / IO37 DT / IO38 SW) is the opposite — mechanical, so its module
debounces via a state-table decoder. Tempo fader: ADC1 on IO8 (ADCT center tap) /
IO9 (ADIN wiper), bare 3.3V pot, no external filtering.

## MIDI contract (`MidiMap.hpp`)
One deck = MIDI channel 1 (0-based `0`). `MidiMap.hpp` is the authority; the Mixxx
controller mapping (`mixxx_config/TriMixxx.midi.xml` + `TriMixxx.scripts.js`) matches it —
change both together, no address overlaps.
- Ring pads: node `i` ↔ note `PAD_BASE + i` (0..49), velocity = white LED brightness.
- Jog: `CC_JOG` relative 7-bit two's-complement ticks; `NOTE_JOG_TOUCH` = scratch enable.
- Tempo: 14-bit CC pair — `CC_TEMPO` (MSB) + `CC_TEMPO_LSB` (= MSB+32); Mixxx must bind both as `<fourteen-bit-msb>`/`<lsb>`.
- Track encoder: `CC_ENCODER` relative (1=up, 127=down) + `NOTE_ENC_SW` press.
- Play/cue: `NOTE_PLAY` / `NOTE_CUE` press; LED ← incoming Note-On velocity.
- Loop: `NOTE_LOOP_IN` / `NOTE_LOOP_OUT` / `NOTE_RELOOP` press; LED ← Note-On (reloop has no LED).

### SysEx (Mixxx → firmware only)
`F0 7D <cmd> <args…> F7`, manufacturer ID `0x7D` (reserved non-commercial). Every
payload byte must be 7-bit — that is a SysEx rule, not a convention. `ttymidi`
carries the payload opaquely (its README: *"System Exclusive — carried opaquely"*),
so this layout is purely between the Mixxx mapping and `main.cpp`'s `onSysExFromMixxx`.

- **`0x01` ring LEDs** — `F0 7D 01 <node> <colour bytes> F7`. Each 8-bit channel is
  sent as **two data bytes, high nibble first** (`v>>4`, `v&0x0F`); that is what keeps
  the full 0..255 per channel, since one 7-bit byte would cap it at 127. Channel order
  R,G,B matches the OneButton wire slot, so nothing is reordered.
  - 12 colour bytes (`R1 G1 B1 R2 G2 B2`) → LED0 and LED1 set independently.
  - 6 colour bytes (`R1 G1 B1`) → one colour **mirrored onto both LEDs**.
  - Wrong length or `node >= RING_A_NODES` → ignored.
- **`0x02` reset** — `F0 7D 02 52 53 54 F7` ("RST") reboots the S3 via `esp_restart()`,
  equivalent to the physical RESET button. The magic is **required**: it stops a stray
  or corrupt SysEx from rebooting the deck mid-set, so a bare `F0 7D 02 F7` does nothing.

Note-On velocity → white pad brightness still works and is unchanged; SysEx is the
way to get an actual colour, which a 7-bit velocity cannot express.

## Status
All deck controls have drivers: **ring A pads**, **jog wheel**, **tempo fader**,
**track encoder**, **play/cue board**, and **loop board**. The Mixxx controller
mapping lives at the monorepo root in **`mixxx_config/`** (`TriMixxx.midi.xml` +
`TriMixxx.scripts.js`, see `mixxx_config/README.md`). A 2nd ring (UART2) is
reserved but not built.

`main.cpp` has a single **`DECK_DEBUG`** switch (top of the file): set to `1`,
`loop()` calls each module's own `debug()` self-test instead of sending MIDI —
ring railroad + magenta-on-press, play/cue + loop LED flash (solid while the
button is held; reloop lights both loop LEDs), and jog/tempo/encoder serial
reports. Set to `0` for normal operation. Each driver owns its `debug()`.

## Conventions
- New peripheral = new self-contained module in `lib/<Name>/`, wired only in `main.cpp`. Keep MIDI/Mixxx knowledge out of drivers.
- Each driver exposes a `debug()` self-test (LED pattern and/or serial report); `main`'s single `DECK_DEBUG` toggle runs them all instead of the MIDI loop. New modules should add one.
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
- **Debounce switches by polling, not a GPIO edge ISR.** Sample on a fixed
  periodic tick and latch the debounced press (the `buttonPollTask` for
  `PlayCue`/`LoopBoard`); don't hang an edge interrupt off a mechanical switch.
  It's the robust approach (Ganssle), and on ESP32 a slow RC-debounced edge can
  make a `FALLING` ISR double-fire (phantom press on release). A human press is
  tens of ms, so a ~2 ms poll never misses a real one.
- **Use hardware peripherals over CPU/ISR** when one exists: PCNT for the jog
  quadrature (zero CPU, nothing missed), UART for the rings.
- **Guard state shared between a FreeRTOS task and `loop()`** — the ring task
  (`OneButtonRing`) and the button poll task (`PlayCueBoard`/`LoopBoard`) — with
  the module's `portMUX` critical section, and mark cross-context flags
  `volatile`. Keep critical sections short. Pin auxiliary tasks to core 1 (the
  ring owns core 0) so the two never contend.
- **Filter noisy analog** — oversample + EMA, and add hysteresis before emitting
  (`TempoFader`). Use **ADC1 only** (IO1–10); ADC2 is unusable with WiFi active.
- **Prefer `constexpr` over `#define`** for typed constants; keep pin numbers as
  `#define`/`constexpr` at the top of `main.cpp`.
- **Fixed loop cadence, no long blocking.** Don't add long `delay()`s to `loop()`;
  latency-sensitive work (jog, MIDI) runs every pass.
