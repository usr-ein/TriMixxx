# The Effect Rack — product requirements

The deck's effects UI: a 19-inch rack of effect modules, on a touchscreen,
driving the effect-pedal bus. This document is the *what* and the *why*.

Status: **revision 2.** Design decisions recorded in §12. One consequence of the
per-unit wet knob (§3.2) needs acknowledging before implementation; the
remaining questions in §13 do not block.

Companion documents: `xone92-send-return.md` for the mixer side,
`worklog/effect-pedal/TODO.md` for the engine work already landed.

---

## 1. Why

The pedal bus works — the Xone's aux send arrives at `[Auxiliary1]`, an effect
unit processes it in WET mix mode, and the result returns on the deck's output
with no dry attached. What it does not have is a way to *play* it.

Today's page is a text read-out with one selectable row and an encoder. That was
built to make the signal path legible while it was being debugged, and it did
that job. It is not an instrument. A DJ reaching for a filter mid-transition
needs to grab the control, not turn an encoder to the fourth row and press it to
enter adjust mode.

**The design intent is a rack of hardware.** Not a flat pane of sliders — a row
of effect modules with screws, bezels, brushed metal and real knobs, each with
its own identity, in the spirit of a Winamp skin: unashamedly skeuomorphic,
readable at arm's length in a dark booth, and fun to look at.

## 2. Scope

**In:** a full-screen horizontal rack; effect modules with drawn chrome and
draggable knobs; a per-unit wet knob; a pinned master module driven by the
encoder; add, reorder and delete; scrolling; save and load of whole racks;
persistence across restarts.

**Out (this round):** LV2 hosting, beat-synced delay times, a second rack for the
deck's own audio, MIDI mapping of rack controls, effect automation.

**Moved, not deleted:** everything the current effects page shows about the
signal path — aux configured, aux to main, aux level, unit routed, unit enabled,
mix mode, effect loaded, effect on, slot group — **moves to Diagnostics**. Plus
the measured round-trip latency and the aux VU as a live bar. The rack shows no
status text.

## 3. Signal flow

```
100% aux in ──> Unit 1 ──> Unit 2 ──> … ──> Unit 6 ──> MASTER ──> deck output
                (wet% +     (wet% +          (wet% +    (level,
                 metas)      metas)           metas)     mute)
```

**The input is fixed at unity.** How much of each channel reaches the rack is set
by that channel's aux send on the Xone, which is where it belongs. There is no
input trim in this UI.

### 3.1 What a unit's wet knob does

Serial, per unit, standard pedal semantics:

```
out_N = in_N · (1 − w_N)  +  fx_N(in_N) · w_N
```

where `fx_N` is that effect's **fully wet** output — the reverb's tail alone, the
echo's repeats alone, the filter's filtered signal. `in_N` is whatever the
previous unit produced, not the original input.

For reverb and echo this needs the chain to stop re-adding the dry itself, which
it already can: `skipAddingDry` exists and fires for any mix mode that is not
DRY/WET. With the per-unit blend doing that job uniformly, every effect in the
rack is treated the same way regardless of whether its manifest declares
`addDryToWet`.

Your example works out as expected: aux → HPF at 100% → filtered; → Reverb at
100% → the reverb of the filtered signal, and nothing else.

### 3.2 The consequence, stated plainly

**The rack can output dry, and will, whenever the last unit in it is not a fully
wet additive effect.** Two cases:

- **Any unit below 100%** passes part of its input through. That input traces
  back to the original aux, so a fraction of the untouched dry reaches the deck
  output — and the mixer adds it to the CDJ's own channel. The doubling we
  removed comes back, in proportion.
- **A filter at 100% is still dry.** A high-pass produces *the same signal,
  filtered* — it is correlated with the dry, not new material. A rack that is
  just an HPF returns a filtered copy of the track, which sums with the CDJ's
  channel. That is a parallel filter, not the inline filter a mixer gives you,
  and with the bus's 32 ms round trip it will comb against the dry with notches
  every 31 Hz.

This is **inherent to per-unit wet on a send/return**, not a defect, and it is
worth knowing rather than discovering. The rack is dry-free only when the DJ
leaves the final additive unit at 100%. Reverb and Echo can be; filters cannot.

If a structural guarantee is wanted instead, the per-unit knob would have to mean
something else — a parallel contribution to an output bus rather than a serial
blend — and the chain would stop being a chain. That is a different instrument.
**Assumption for now: the serial blend above, and the freedom that comes with
it.**

## 4. Engine work required

None of this is skin work.

| Change | Where | Size |
|---|---|---|
| `kNumEffectsPerUnit` 4 → 6 | `src/effects/defs.h:41` | One constant; it sizes `EffectStatesMapArray`, a `std::array` of per-slot engine state |
| **Per-slot wet control** | `EffectSlot` + `EngineEffectChain::process` | New `ControlPotmeter` per slot, shipped to the engine, blended in the chain loop |
| Per-slot wet in presets | `EffectPreset` | So saved racks restore their blend |
| Confine `WetOnly` to the toggle it belongs to | `EffectChain` | `kNumModes = 3` currently leaks `WET` into the mix-mode cycle of QuickEffect and EQ chains, where it is meaningless and would kill the dry |

**The per-slot blend applies only in chains whose mix mode is `WET`.** That keeps
the new behaviour inside our rack and leaves deck, QuickEffect and EQ chains
byte-identical to upstream.

## 5. The rack

Full screen, no header — the same treatment the browser gets, since this is
reached from the browser's root menu.

```
┌────────────────────────────────────────────────┬──────────┐
│ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐    │          │
│ │        │ │        │ │        │ │        │    │  MASTER  │
│ │ REVERB │ │ DELAY  │ │  HPF   │ │  ECHO  │ (+)│          │  544
│ │        │ │        │ │        │ │        │    │  pinned  │
│ └────────┘ └────────┘ └────────┘ └────────┘    │          │
├────────────────────────────────────────────────┴──────────┤
│                    (bezel dead strip)                     │  56
└───────────────────────────────────────────────────────────┘
  ←──────────── scrolls ────────────→              always visible
```

- **Module size:** 204 × 544, four-pixel gutters.
- **The master module is pinned to the right edge** and never scrolls — it is
  always reachable. The remaining 820 px scrolls and holds the units plus the
  `(+)` at its right end, so four units are visible at a time and six fit with
  scrolling.
- **Signal order is left to right**, identical to chain slot order.
- **The bottom 56 px is dead**, as everywhere on this deck: the panel's lower
  edge sits under the chassis bezel.
- **Empty rack:** the `(+)` alone, centred in the scroll area, with `Add an
  effect` beneath it. The master stays.

## 6. A unit module

```
┌─────────────────────┐  ← 2 px bezel, module-specific
│ ◉                 ◉ │  ← screws, drawn into the chrome
│      ╭───────╮      │
│      │  ███  │      │  ← WET: the unit's contribution
│      ╰───────╯      │
│         WET         │
│    ╭─────╮ ╭─────╮  │
│    │ ██  │ │  ██ │  │  ← the effect's metaknobs
│    ╰─────╯ ╰─────╯  │
│     DECAY    TONE   │
│  ╭─────────────╮    │
│  │   REVERB    │    │  ← stylised name, bottom left
│  ╰─────────────╯    │
│ ◉                   │
└─────────────────────┘
```

- **WET is the largest knob** and sits top-centre. It is what gets reached for.
- **Below it, the effect's own metaknobs**, however many that effect has, minus
  any the module hides (§9).
- **Name plate, bottom left**, in the module's own typeface treatment — engraved
  into metal, silkscreened onto plastic. The name is part of the skin.
- **No bypass.** WET at zero is the bypass, and it fades rather than cuts.
- **Screws are drawn, not images** — part of the chrome layer.

### Rendering

Each module's chrome — texture, bezel, screws, name plate — is **painted once
into a `QPixmap` and cached**, keyed by type and size. Only knobs repaint on
interaction, into that cached backdrop, confined to the knob's own rect. The deck
is a Pi 4 with no compositor help for widgets; regenerating a brushed-metal
gradient on every knob movement would be visible.

## 7. The master module

Pinned right, always visible, and the only module the **encoder** touches.

- **Encoder rotate** — master output level. This is the chain's `mix` control in
  WET mode: how much of the rack reaches the deck's output.
- **Encoder press** — mute / unmute. Unmuting returns to the level that was set,
  not to a default.
- Shows the level as a large knob or fader, and the mute state unmistakably —
  this is the control that stops the effects mid-set.

The encoder does nothing else on this page. Rack scrolling is touch-only.

## 8. Interaction

| Gesture | Effect |
|---|---|
| **Drag a knob up/down** | Turn it. Vertical only, so a sloppy diagonal still turns cleanly. |
| **Double-tap a knob** | Reset to the parameter's default. |
| **Drag anywhere else** | Scroll the rack horizontally. No scrollbar, kinetic, clamped. |
| **Long-press a module** | Border becomes a dashed outline: held. Then drag to reorder, or drag onto the bin to remove. |
| **Tap `(+)`** | Effect chooser. Hidden entirely when the rack is full. |
| **Encoder** | Master level; press to mute. |

**Knob sensitivity:** full travel over roughly 200 px of vertical drag — fine
control in one movement. Values move in parameter space, so a given drag is the
same fraction of travel whatever the control's units.

**Reordering is a chain reorder**, not a visual one: dropping writes the new slot
order and audio follows immediately.

**The bin** appears at bottom-centre only while a module is held, and only then.
Dropping a module onto it removes it from the rack. Everything else is
unreachable while dragging.

## 9. The effects, and their skins

| Module | Material | Engine | Knobs shown |
|---|---|---|---|
| **Reverb** | Brushed metallic grey — horizontal brush grain, vertical sheen, darker at the edges | `reverb` | WET + decay, bandwidth, damping |
| **Delay** | Yellow plastic, flat and slightly glossy | `echo` | WET + time *(feedback and the rest hidden and preset)* |
| **Echo** | Green plastic, same treatment, different hue | `echo` | WET + time, feedback, ping-pong |
| **HPF** | Slick shiny black, piano gloss with a strong specular | `filter` | WET + high-pass cutoff *(lpf and q hidden and preset)* |
| **LPF** | Slick shiny black, as HPF | `filter` | WET + low-pass cutoff *(hpf and q hidden and preset)* |

Knob styling follows the material: metal knobs on metal, plastic on plastic.

**HPF and LPF are two instances of the same `filter` effect**, each with the
irrelevant parameters pinned open and hidden. Same for **Delay and Echo**, both
`echo`, differing in which knobs are exposed and where the hidden ones sit. A
module is therefore a *preset plus a skin*, not necessarily a distinct effect.

## 10. Saving and loading racks

Mixxx already has this and the deck already stores it on the SD card. Named chain
presets are XML files in `~/.mixxx/effects/chains/`, managed by
`EffectChainPresetManager`, with `savePreset`, `loadChainPreset` and the
`next_chain_preset` / `prev_chain_preset` / `chain_preset_selector` controls
already wired.

So a saved rack is a stock Mixxx chain preset, and no new file format is needed.
The only additions are the per-slot wet value (§4) and a way to reach it:

**Proposal:** tapping the master module's name plate opens a rack browser over
the screen — the saved racks by name, plus `Save as…`. Loading swaps the whole
rack, including the master level.

## 11. Persistence

The live rack lives in `effects.xml`, which `mixxx_config/upload.sh` already
ships. This round-trips correctly **provided effects are loaded from their
manifest**: a preset carrying an empty `<Parameters>` list produces an effect
that reports itself loaded with every parameter at zero, and then re-serialises
that broken state on exit.

The bootstrap in `TriMixxx.setupPedalBus()` exists because of that trap and
should be removed once the rack manages its own chain.

## 12. Decisions

| # | Question | Answer |
|---|---|---|
| 1 | Per-unit wet | **Yes** — every unit has its own wet knob, serial blend (§3.1) |
| 2 | Global wet | **Master module, pinned rightmost**, encoder-driven, press to mute |
| 3 | `kNumEffectsPerUnit` | **6** |
| 4 | HPF / LPF | **Two `filter` instances**, irrelevant knobs preset and hidden |
| 5 | Delay / Echo | **Both `echo`**, Delay hides the extra knobs |
| 6 | Input trim | **None** — always 100%; the Xone's per-channel sends do this |
| 7 | Per-module bypass | **No** |
| 8 | Full rack | `(+)` **hidden**; removal is drag-to-bin |
| 9 | Save/load | **Mixxx chain presets**, on the SD card, existing machinery |

## 13. Remaining questions

1. **§3.2 — is the dry consequence accepted?** Per-unit wet means the rack
   returns dry in proportion whenever a unit sits below 100%, and a filter-only
   rack returns dry by definition. I assume yes, and that it is the DJ's to
   manage. Say if not.
2. **Where does the rack browser live?** Proposal is a tap on the master's name
   plate (§10). Somewhere better?
3. **Textures procedural or PNG?** Procedural keeps the fork asset-free and
   scales to any size. **Send the reference PNGs either way** — I can match them
   procedurally from a picture.
4. **Does the master's mute need to survive a restart?** Simplest is no: it is a
   performance control, and a deck that boots muted is a support call.
5. **Two `filter` instances cost two of the six slots.** With HPF, LPF, reverb
   and echo that is four of six. Is six still right, or should it be eight?
