# The Effect Rack — product requirements

The deck's effects UI: a 19-inch rack of effect modules, on a touchscreen,
driving the effect-pedal bus. This document is the *what* and the *why*.

Status: **revision 4 — built.** The rack is implemented and on the deck
(`src/widget/deck/wdeckrack.cpp`). §14 records what was compromised, deferred or
decided differently once it met the code.

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

### 3.2 The first dry-killer is locked to 100%

A serial blend passes part of its input through, so an unconstrained rack would
hand the original dry back to the mixer in proportion to how far the knobs are
down — the +6 dB doubling this whole project removed, available again by
accident. The rule that prevents it:

> **The first unit in the rack that generates new material has its wet knob
> locked at 100%. Every unit after it blends freely.**

The dry dies at that unit and cannot come back, because everything downstream is
blending two signals that are both already dry-free. A later reverb at 50% mixes
half echo with half delay — never half of the original track.

**"Generates new material" is `addDryToWet` in the effect's manifest**, and it
needs no new classification. Reverb and Echo declare it precisely because their
output contains none of the direct signal — a tail, a train of repeats. Filters,
distortion, bitcrusher and pitch shift do not, because their output *is* the
input, transformed. So the flag already means exactly "this effect removes its
input from its output", which is the property the rule turns on.

Worked through:

| Rack | Locked | Result |
|---|---|---|
| `HPF → Reverb → Delay` | Reverb | HPF passes dry; Reverb destroys it; Delay blends dry-free signals |
| `Reverb → HPF → Delay` | Reverb | Dry dies immediately; the filter and delay pass only dry-free material |
| `Reverb → Delay` | Reverb | Delay is free to sit at 40% and blend reverb with delayed reverb |

**Filters early are fine**, which is what makes this rule better than a blanket
one: an HPF at the head shapes what the reverb hears, passes the dry along, and
the reverb then throws that dry away. Exactly the useful case.

**The lock moves.** It is a property of position, not of a unit, so reordering
re-evaluates it. When a unit becomes the first dry-killer its wet jumps to 100%;
when it stops being one, it returns to the value it had before it was locked.

**The one case the rule cannot save:** a rack containing *no* dry-killer at all —
all filters, say — returns a correlated copy of the track, which sums with the
CDJ's channel and combs against it every 31 Hz at the bus's measured 32 ms. That
is a parallel filter and it is what the DJ asked for by building that rack. Worth
knowing; not worth preventing.

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
┌───────────────────────────────────────────────────────────┐
│  HPF → RVB → DLY                             2026-08-11   │  48  ← name bar
├────────────────────────────────────────────────┬──────────┤
│ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐    │          │
│ │        │ │        │ │        │ │        │    │  MASTER  │
│ │ REVERB │ │ DELAY  │ │  HPF   │ │  ECHO  │ (+)│          │  496
│ │        │ │        │ │        │ │        │    │  pinned  │
│ └────────┘ └────────┘ └────────┘ └────────┘    │          │
├────────────────────────────────────────────────┴──────────┤
│                    (bezel dead strip)                     │  56
└───────────────────────────────────────────────────────────┘
  ←──────────── scrolls ────────────→              always visible
```

- **Module size:** 204 × 496, four-pixel gutters.
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
- **When a unit is the first dry-killer (§3.2) its WET knob is locked** at full
  and does not turn. It must *look* locked rather than broken: the pointer sits
  at maximum, the cap is drawn differently — a slotted screw-head instead of a
  grip, say — and the caption reads `WET · LOCKED`. A knob that silently refuses
  to move is a fault report; a knob that is visibly bolted down is a design.
  Dragging it does nothing, and double-tap does nothing.
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
| **Reverb** | Brushed metallic grey | `reverb` | WET + decay, bandwidth, damping |
| **Delay** | Yellow plastic | `echo` | WET + time *(the rest hidden and preset)* |
| **Echo** | Green plastic | `echo` | WET + time, feedback, ping-pong |
| **HPF** | Piano-gloss black | `filter` | WET + high-pass cutoff *(lpf, q hidden)* |
| **LPF** | Piano-gloss black | `filter` | WET + low-pass cutoff *(hpf, q hidden)* |

**HPF and LPF are two instances of the same `filter` effect**, each with the
irrelevant parameters pinned open and hidden. Same for **Delay and Echo**, both
`echo`. A module is a *preset plus a skin*, not necessarily a distinct effect.

### 9.1 Assets: procedural, from sampled palettes

The reference skins were examined. **Nothing can be lifted directly**, for a
reason that is structural rather than legal: a Winamp skin is a *whole window
baked into one bitmap at one fixed size*. `main.bmp` is 275 × 116 with the title
bar, LCD and every transport button flattened into it; `gen.bmp` is a window
frame at 194 × 109. Our modules are 204 × 544. There are no tileable textures and
no scalable components to extract, and the round buttons that come closest to a
knob cap are 18 px where we need 60.

There is also no licence on any of them, and the fork is GPL, so shipping the
bitmaps would be a small but real problem for no gain.

**So: drawn procedurally, using palettes sampled from the skins and the
construction grammar they all share.** That gives the exact colours, works at any
size, and keeps the fork asset-free.

### 9.2 The grammar

Four rules, taken from the skins, that make flat pixels read as objects:

1. **A bevel is two lines.** Light on the top and left edge, dark on the bottom
   and right, over a mid-tone face. Winamp98 uses `#FFFFFF` / `#C0C0C0` /
   `#808080` exactly. Swap the two lines and the same shape reads as *recessed* —
   which is how knob troughs and screw holes are drawn.
2. **Engraved text is two passes.** The glyph in a dark tone, then the same glyph
   offset one pixel down-right in a light tone. On metal it looks stamped; it is
   also what keeps small text legible at arm's length.
3. **Gloss is one specular arc.** A single bright ellipse in the upper-left third
   of a round object, fading out. Purple Glow's near-black face at `#0B0B0B` gets
   its depth almost entirely from this.
4. **Panels get a vertical light gradient** — lighter at the top, darker at the
   bottom — regardless of material. It is what stops a large flat area looking
   like a rectangle of colour.

### 9.3 The materials

| Material | Base | Highlight | Shadow | Treatment |
|---|---|---|---|---|
| **Brushed steel** (Reverb) | `#AFB6C2` | `#CFD2DB` | `#A8AFBC` | Vertical gradient, then horizontal 1 px grain of ±4 % lightness noise. Grain is the whole trick — it is what separates brushed metal from grey. |
| **Yellow plastic** (Delay) | `#E8C51F` | `#F7E27A` | `#8A7410` | Vertical gradient, no grain, a soft gloss band across the top third. Moulded, not machined. |
| **Green plastic** (Echo) | `#3FA64B` | `#8FD897` | `#1E5A26` | As Delay. |
| **Piano gloss** (HPF/LPF) | `#0B0B0B` | `#414141` | `#000000` | Near-black, one hard specular streak down the upper left, and a bevel bright enough to catch the eye against the black. |

**LCD blue** `#284A89` on `#002263`, with the dot-matrix texture, is reserved for
the rack name bar (§10) — the one place on screen that displays rather than
controls, exactly as in the skins.

Knob caps follow their module: machined aluminium on steel, moulded plastic on
plastic, gloss on gloss.

## 10. Saving and loading racks

Mixxx already has this and the deck already stores it on the SD card. Named chain
presets are XML files in `~/.mixxx/effects/chains/`, managed by
`EffectChainPresetManager`, with `savePreset`, `loadChainPreset` and the
`next_chain_preset` / `prev_chain_preset` / `chain_preset_selector` controls
already wired.

So a saved rack is a stock Mixxx chain preset, and no new file format is needed.
The only addition is the per-slot wet value (§4).

### 10.1 The name bar

A 48 px strip across the top of the screen, drawn as an LCD panel (`#284A89` on
`#002263`, dot matrix), showing the rack's name on the left and its creation date
on the right.

**The name is generated, never typed.** It is the modules' abbreviations in
signal order joined by arrows:

```
HPF → RVB → DLY                                          2026-08-11
```

Three letters each: `RVB` `DLY` `ECH` `HPF` `LPF`. A deck with no keyboard should
not ask for one, and a rack's identity really is its contents — the date
disambiguates two racks built from the same modules.

An unsaved rack shows its generated name with no date, so "not yet saved" needs
no extra indicator.

**Tapping the bar opens the rack browser** over the screen: saved racks by name
and date, plus `Save this rack`. Loading swaps the whole rack, including the
master level. Long-pressing a row in the browser deletes it, with the same dashed
-border confirmation idiom the modules use.

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
| 1 | Per-unit wet | **Yes** — serial blend (§3.1), except the first dry-killer, whose knob is **locked at 100%** (§3.2) |
| 2 | Global wet | **Master module, pinned rightmost**, encoder-driven, press to mute |
| 3 | `kNumEffectsPerUnit` | **6** |
| 4 | HPF / LPF | **Two `filter` instances**, irrelevant knobs preset and hidden |
| 5 | Delay / Echo | **Both `echo`**, Delay hides the extra knobs |
| 6 | Input trim | **None** — always 100%; the Xone's per-channel sends do this |
| 7 | Per-module bypass | **No** |
| 8 | Full rack | `(+)` **hidden**; removal is drag-to-bin |
| 9 | Save/load | **Mixxx chain presets**, on the SD card, existing machinery |
| 10 | Rack name | **Generated**, not typed: `HPF → RVB → DLY` plus creation date, in an LCD bar across the top (§10.1) |
| 11 | Rack browser | **Tap the name bar** |
| 12 | Mute across restarts | **No** |
| 13 | Assets | **Procedural**, from palettes sampled off the reference skins (§9.1) |

## 13. Remaining questions

None blocking. Everything above is decided; what is left is settled by drawing it
and looking at it on the deck:

1. **Do 204 px modules hold four knobs legibly** at arm's length in a dark booth,
   or does the count have to come down? Settled by rendering one, not by
   argument.
2. **Is a 200 px drag the right knob throw** for the WET knob specifically? It is
   the one that gets grabbed mid-transition and may want to be coarser.
3. **Does the brushed-metal grain survive** at 204 px wide without looking like
   noise? If not, the grain gets coarser rather than the material changing.


---

## 14. What the build changed, deferred or compromised

Written after implementing it, because a PRD that never gets marked up is a PRD
nobody checked against.

### Deferred

**Saving and loading named racks (§10) is not built.** The design stands and
Mixxx already has the machinery — chain presets as XML under
`~/.mixxx/effects/chains/`, via `EffectChainPresetManager`. The obstacle is
plumbing rather than design: saving needs `EffectsManager`, and a skin widget is
handed no route to it. Doing it properly means threading that through
`LegacySkinParser` into `WDeckBrowser`, which is a change to how the deck's
widgets are constructed and did not belong in the same commit as the drawing.

Loading *could* be done today through `chain_preset_selector` and friends, which
are ordinary controls — but a browser that can load and not save is worse than
none. **The name bar is built and shows the generated name; tapping it does
nothing yet.**

The live rack still persists across restarts through `effects.xml`, which now
works: the shutdown crash that prevented Mixxx ever writing its settings was
fixed on the way here.

### Compromised

**The dry-killer test is a flag in the module catalogue, not the manifest.**
§3.2 defines "generates new material" as `addDryToWet`, which is exactly right
and is what the engine uses. The widget cannot see manifests — it reaches the
engine through `ControlProxy` and there is no control that exposes the flag — so
`CatalogueEntry::generatesNewMaterial` restates it. Two places to keep in step,
for a catalogue that is five entries long and fixed. If the rack ever takes
arbitrary effects, this has to come from the manifest instead.

**Effects are loaded by position in `VisibleEffects`, not by id.**
`loaded_effect` takes an index, and no control takes an effect id. So the
catalogue carries indices into the list shipped in `mixxx_config/effects.xml`
(reverb 3, filter 14, echo 15). It is the one coupling from this code to a file
it does not own, and reordering that list silently loads the wrong effects.
Called out in a comment where the table is.

**Knobs stack rather than sitting side by side.** §6 sketched two knobs per row
below the metaknob. At 204 px, two knobs with captions readable at arm's length
do not fit, so all knobs are in one column: the big one at the top, the rest
under it. It costs vertical room and caps a module at about four knobs, which is
enough for everything in the catalogue.

**Scrolling is direct, not kinetic.** The rack follows the finger and stops when
it stops. Kinetic scrolling with a `QScroller` would fight the long-press and
the knob drags for the same events, and the rack is at most six modules wide —
about one screen and a half — so there is nothing to fling through.

### Decided differently

**No per-module bypass, and the metaknob is not a separate knob.** §12 settled
bypass as "no". In the same spirit the module's headline knob is its **WET**,
not the effect's metaknob: WET is what the PRD asks for per unit, it is what the
engine now implements per slot, and having both would be two knobs that mostly
do the same thing. Each effect's real parameters are exposed directly instead —
decay, bandwidth and damping rather than one macro over them.

**Turning the encoder while muted sets the level it will return to**, rather
than unmuting. Unmuting by turning would be a surprise; this way mute is only
ever left deliberately, with a press.

**`(+)` is hidden when full, and an empty rack shows it centred** with "Add an
effect" beneath, as §5 asks. The bin appears only while a module is held.

### What looking at it changed

Screenshotted on the deck with `pi_config/deck-shot`, which is what §13 was for.
Three of the guesses were wrong.

**The module is not 496 px tall.** The browser draws a breadcrumb above the page,
so the rack gets about 355. The last knob hung out of the bottom of the panel and
the name plate lay across the one above it. Nothing is a constant now:
`knobGeometry()` derives every position from the module rect it is given, and the
row pitch is solved from the space actually left, so the layout cannot overflow
whatever height the panel turns out to be.

**The name belongs at the top, not the foot.** §6 put it bottom-left, following
the reference skins' name plates. On a tall narrow module the eye lands at the
top and the plate at the bottom was competing with a knob. It is now engraved
into a hatched title strip across the head of the module, with a lit pilot beside
it.

**Knobs stack in two staggered columns, not one.** The single column of §14's
first draft was legible but wasteful, and four knobs did not fit. Two columns
with the right one dropped half a row buys the room back: the captions of one
column sit beside the knobs of the other rather than under them.

**Knob caps must not be their panel's colour.** This one only appeared with three
materials on screen at once — matching made the black module's knobs invisible
and the yellow module's gold-on-gold. Real equipment goes the other way, dark
caps on light panels and light on dark, and now so does this.

Also added because the first pass read as flat: a frame within a frame, the
working area recessed into the face, ribbed hatching on the title strip, tick
marks around each knob's travel, knurling on the caps, a corner grip. One bevel
looks like a button; three depths look like a panel.

### Still unverified

The **touch gestures** — knob drag, double-tap reset, rack scroll, long-press
reorder, drag-to-bin — have not been exercised by a finger. They are implemented
and the geometry they hit-test against is the geometry that is drawn, but a
screenshot cannot press anything.
