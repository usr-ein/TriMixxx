# The Effect Rack — product requirements

The deck's effects UI: a 19-inch rack of effect modules, on a touchscreen,
driving the effect-pedal bus. This document is the *what* and the *why*.

Status: **revision 5 — first test run done.** §15 is the list it produced.

Previously: **revision 4 — built.** The rack is implemented and on the deck
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

**Out (this round):** LV2 hosting, a second rack for the deck's own audio, MIDI
mapping of rack controls, effect automation.

**Beat sync is in, and was out when this was written.** The aux has no beatgrid
of its own, so `[EffectTempo] bpm` is published from whichever deck has been
playing longest — Pro DJ Link players and this deck judged on the same terms —
and `EngineAux::collectFeatures` turns it into a `beat_length`. Length only, not
phase: quantising a delay to a musical division is the useful half, and the
phase of several decks summed by a mixer and arriving 32 ms late has no single
right answer. The name bar says which deck and at what tempo.

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

### Deferred, then built after all

Saving and loading named racks was written up here as deferred, on the grounds
that saving needs `EffectsManager` and a skin widget has no route to one. **That
was wrong** — `LegacySkinParser` has held an `EffectsManager*` all along, so the
plumbing was one argument passed to `WDeckBrowser` and on to the rack.

Everything else was already in Mixxx: named chain presets as XML under
`~/.mixxx/effects/chains/` (the SD card on this deck), loaded at startup, listed
sorted, deleted on request. A rack *is* a chain, so saving one is saving a chain
preset. It also writes immediately, so a saved rack does not depend on Mixxx
surviving to shutdown.

Tapping the name bar opens the list; row 0 is always "save this one", so an
empty list is a usable screen rather than a dead end.

**Loading forces the mix mode back to `WET` afterwards.** A preset carries its
own, and a rack restored as `DRY/WET` would quietly put the dry into a mixer
that already has one — the exact fault this bus exists to remove, arriving
through the one door nobody would think to watch.

The live rack also persists through `effects.xml`, which works now that the
shutdown crash is fixed.

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


---

## 15. Follow-up from the first test run

Nine problems, in the order they were reported. Several are one-line fixes;
two are engine work; one is a design mistake in this document.

### 15.1 The rack cannot reach the deck's own audio — **engine work**

The rack processes `[Auxiliary1]` and nothing else, so it can effect every
channel on the mixer *except* the one the deck is playing. That is backwards for
the one deck the DJ is actually holding.

Wanted:

```
(AUX IN × a) + (TriMixxx deck × b) ──> rack ──> wet ─┐
                                                     ├──> deck output
                             TriMixxx deck dry ──────┘
```

**Routing the unit to both channels does not do this.** Mixxx effect units are
per-input-channel: a unit enabled for `[Auxiliary1]` and `[Channel1]` processes
each *separately*, with its own state per channel — and in WET mode the deck's
channel would then emit tail only, taking its dry with it. What is wanted is one
chain over a *sum*, which Mixxx has no concept of.

**Proposed: make the aux channel the send bus.** `EngineAux::process()` already
receives the input, applies `pregain`, and runs the chain. If it also summed in
a scaled copy of the deck's post-fader audio *before* the chain, then everything
downstream is unchanged: the same single chain, the same WET output, the same
one place it lands in the main mix — and the deck's own dry path is never
touched, so it keeps going to the output as it does now.

The risk is ordering: the deck's buffer has to be filled before the aux is
processed. `EngineMixer` decides that, so this needs checking rather than
assuming, and a wrong order silently gives a buffer late by one callback (~6 ms
— inaudible, but wrong, and the kind of wrong that is never noticed).

**The two dials** are that bus's send levels — a fixed **INPUT** module pinned at
the far left, mirroring the master pinned at the far right, with `AUX` and
`DECK`, both defaulting to 100%. They are exactly the Xone's per-channel aux
sends, one layer in.

### 15.2 The master should choose between cutting and ringing out

Turning the master down cuts the wet immediately, because it scales the chain's
*output*. To let a reverb ring out, the master has to scale the chain's *input*
instead and let the tails decay on their own.

That is the send/return distinction from `xone92-send-return.md`, arriving
inside the deck: pull the **send** and the tank stops being fed but what is in
it rings out; pull the **return** and the tail is cut dead. Both are musical and
they are different gestures.

**A toggle on the master module, drawn as a rocker** — `(0  )` / `(  0)` — picks
which of the two the master knob and the mute drive. The other stays at unity.
Default: **ring out**, because it is the one that cannot be got any other way.

### 15.3 A VU on every module, and on the master — **engine work**

Nothing publishes an effect slot's output level; it has to be measured where the
audio is, in `EngineEffectChain::process()`, after each effect. A peak per slot,
published as a control the module can draw.

Styled to match: a recessed slot with a segmented ladder, green through amber to
red, the way the reference skins draw theirs — not a flat progress bar.

### 15.4 The rack is too quiet even at full master

Correct, and structural rather than a mistake: the chain's output is
`wet × mix`, `mix` maxes at 1.0, and a reverb tail at unity is far quieter than
the dry it is sitting beside. There is no gain in the path at all.

**Needs makeup gain**, either as range on the master beyond unity (say to
+12 dB) or as a normalising stage on the wet. The master having gain in hand is
the more useful of the two — "I can always turn it down" only works if there is
something above unity to turn down from.

### 15.5 The rack does not survive leaving the page — **root cause found**

Not persistence. `writeChainToEngine()`:

```cpp
m_modules.append({i, m_modules.size()});   // slot is already the index it will take
...
if (module.slot != i) {                    // so this is false
    slotControl(i, "loaded_effect")->set(...);   // and the effect never loads
}
```

A module added from the chooser is **drawn but never loaded into the chain**.
Leaving the page calls `syncFromEngine()`, which rebuilds from the chain, finds
nothing, and empties the rack. Reordering works only because moving a module
makes `slot != i` true, which is why the fault looked intermittent.

Also in this area:

- **Saving asks for a name.** `EffectChainPresetManager::savePreset()` puts up a
  dialog. On a deck with no keyboard the name must be generated and the file
  written directly.
- **A saved rack does not appear in the list** — almost certainly the same
  thing: the dialog is dismissed, nothing is saved.
- **The list does not scroll.** It draws rows until it runs out of screen and
  stops.
- **The last saved rack should load at startup**, so the deck comes up as it was
  left.

### 15.6 Filters should not have a wet knob at all — **the design was wrong**

The report is right, and this document is what was wrong. §3.1 applies one blend
rule to every module:

```
out = filter(in, cutoff)·w + in·(1 − w)
```

Everything above an HPF's cutoff appears in **both** terms — once filtered, once
not — so it is heard twice, at a level that depends on the wet knob. That is not
a half-open filter, it is a comb of the passband against itself.

**A filter's output is the whole of its output:**

```
out = filter(in, cutoff)
```

No wet knob. "No effect" is what the cutoff already means at its extreme — HPF
at the bottom, LPF at the top — so the knob it needs is the one it has.

Two consequences: **the cutoff is a frequency in Hz**, and **HPF and LPF run
opposite ways** — HPF cuts below its cutoff, LPF cuts above, exactly as the Xone
does, so a DJ's hands already know which way to turn. And both **default to
mid-band** rather than to an extreme, so a filter dropped into the rack does
something instead of nothing.

Note this does not disturb §3.2: a filter never was a dry-killer, and removing
its wet knob does not change which module is.

### 15.7 A dragged module jumps to a different height

The drag offsets the module so its *centre* lands under the finger. It should
keep the grab point: record where in the module the press landed and hold that
constant, so the module stays exactly where it was picked up.

### 15.8 Reordering should displace, not just drop

Wanted: with A B C, holding C and dragging it left of B makes **B slide right
into C's place and C take B's**, live, while still held. Dragging further past A
swaps again. Dropping just stops. The rack is always showing the order that
would result, so there is no moment where the outcome has to be imagined.

That is a continuous reorder rather than a deferred one: the swap happens when
the dragged module's centre crosses a neighbour's, and the neighbours animate to
their new positions.

### 15.9 Delay and Echo time should be musical divisions

A continuous time knob is a blur to aim at. Fixed stops, with the value shown
beside the dial:

`1/16 · 1/8 · 1/4 · 1/2 · 3/4 · 1 · 2 · 4 · 8`

These are beats, which is what makes them meaningful now that the bus has a
tempo (§2). Note the floor: at 128 BPM a 1/16 is 29 ms, shorter than the bus's
own 32 ms round trip, so the shortest divisions cannot be compensated for
latency and will sit late. Worth having anyway — a late 1/16 is still a 1/16 —
but it is why the list starts where it does rather than lower.


---

## 16. The FX strip on the deck view

Reaching the effects should not mean leaving the waveform. A DJ riding a filter
through a transition is watching the track, not a menu three levels into the
browser.

So: a narrow **FX** section down the left of the waveform, on the deck view.

```
┌──────┬────────────────────────────────────────────────────┐
│  FX  │                                                    │
│      │                                                    │
│  ▓▓  │              scrolling waveform                    │
│  ▓▓  │                                                    │
│  ██  │                                                    │
│ (◕)  │                                                    │
│ ⟨0 ⟩ │                                                    │
└──────┴────────────────────────────────────────────────────┘
```

- A **VU** showing what the rack is putting out, in the same segmented ladder
  style as the modules (§15.3).
- The **master knob**, the same control the rack's master module drives.
- The **mute mode rocker** from §15.2 — cut, or ring out.

### Touch to focus, and what that costs

Touching the section **claims the encoder**: rotate becomes the FX master level,
press becomes its mute. The section's border lights while it holds focus, so
there is never a question of what the encoder is about to do. Touching anything
else releases it and the encoder goes back to what it does today — the library
when the browser is up, waveform zoom when it is not.

This is the same problem the browser's pages had, solved the same way: something
on screen claims a gesture and says so, rather than every handler knowing about
every claimant. `DeckPage` is that mechanism for the browser stack; the deck view
needs its own small version of it, or the two want merging.

**The cost is the encoder press.** Over the deck view it opens the library, and
while FX holds focus it will not — a DJ who focuses FX and then reaches for the
library has to touch elsewhere first. That is a real consequence and it is worth
deciding rather than discovering:

> **Open question.** Does focus time out — say five seconds after the last touch
> — or is it strictly until something else is touched? A timeout means the
> encoder always comes back on its own; no timeout means it stays where it was
> put, which is better mid-transition and worse the next time you reach for the
> library without looking.

### What it needs first

Both from §15, and neither exists yet:

- **§15.3**, per-slot and master output metering, or the VU has nothing to draw.
- **§15.2**, the mute-mode toggle, or the rocker has nothing to switch.

### What it implies

The rack draws its knobs, VUs, bevels and engraved captions with static methods
inside `WDeckRack`. The strip needs the same vocabulary in a different widget, so
**that painting wants factoring out** — a small `deckchrome` of bevel, knob, VU
and engraved text that both use. Doing it when the second caller appears is the
right time; doing it before would have been guessing at what the second caller
needed.
