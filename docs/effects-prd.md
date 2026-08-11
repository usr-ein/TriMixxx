# The Effect Rack — product requirements

The deck's effects UI: a 19-inch rack of effect modules, on a touchscreen,
driving the effect-pedal bus. This document is the *what* and the *why*.

Status: **draft — not signed off.** Open questions in §11 block implementation;
everything above them is a proposal, not a decision.

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
needs to see and grab the control, not turn an encoder to the fourth row and
press it to enter adjust mode.

The deck has a 1024×600 capacitive touchscreen that, outside the browser, does
almost nothing. This is what it is for.

**The design intent is a rack of hardware.** Not a flat pane of sliders — a row
of effect modules with screws, bezels, brushed metal and real knobs, each with
its own identity, in the spirit of a Winamp skin: unashamedly skeuomorphic,
readable at arm's length in a dark booth, and fun to look at. The deck already
takes this position everywhere else — a CDJ's jog wheel and pitch fader are
physical objects — so its effects should be too.

## 2. Scope

**In:** a full-screen horizontal rack; effect modules with drawn chrome and
draggable knobs; add, remove and reorder; scrolling when the rack outgrows the
screen; a per-effect visual identity; persistence across restarts.

**Out (this round):** LV2 hosting, beat-synced delay times, a second rack for
the deck's own audio, MIDI mapping of rack controls, effect automation.

**Moved, not deleted:** everything the current effects page shows about the
signal path — aux configured, aux to main, aux level, unit routed, unit enabled,
mix mode, effect loaded, effect on, slot group — **moves to the Diagnostics
page**. It is diagnostic information and it belongs with the rest of it. The
rack shows no status text.

## 3. The engine's shape, and what it costs us

The design has to fit Mixxx's effect model or pay to change it. Three places
where it does not fit naturally, stated up front because they drive §11.

**A chain has four slots.** `kNumEffectsPerUnit = 4` (`src/effects/defs.h:41`),
and it sizes `EffectStatesMapArray`, a `std::array` of per-slot engine state. A
rack that scrolls implies more than five modules, so this constant has to rise.
Bounded work, but it is engine work, not skin work.

**There is one wet control per chain, not per effect.** `mix` belongs to the
`EffectChain`. Mixxx's per-effect equivalent is the slot's **metaknob** (`meta`),
which the manifest links to whichever parameters matter — for the reverb, that is
`send_amount`. So a module's headline knob maps to its metaknob, and the chain's
`mix` is a single global wet for the whole rack. Per-module wet-and-dry blending
does not exist and would be a substantial patch.

**Some modules do not correspond to one effect.** Mixxx's `Filter` is a single
effect carrying `lpf`, `q` and `hpf` together; there is no separate high-pass and
low-pass. And there is no `Delay` at all — `Echo` is the only delay line, with
`feedback_amount` at 0 giving a single repeat. Presenting HPF, LPF, Delay and
Echo as four distinct modules therefore means either four presets over two
effects, or new builtins.

**What does fit:** per-slot `enabled` gives each module a real bypass; slot order
is chain order and is already left-to-right; and `effects.xml` round-trips
parameters correctly provided effects are loaded from their manifest.

## 4. The rack

Full screen, no header — the same treatment the browser gets, since this is
reached from the browser's root menu.

```
┌──────────────────────────────────────────────────────────────────────────┐
│ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐                   │
│ │        │ │        │ │        │ │        │ │        │                   │
│ │ REVERB │ │ DELAY  │ │  HPF   │ │  ECHO  │ │  LPF   │        (+)        │  544
│ │        │ │        │ │        │ │        │ │        │                   │
│ └────────┘ └────────┘ └────────┘ └────────┘ └────────┘                   │
├──────────────────────────────────────────────────────────────────────────┤
│                        (bezel dead strip)                                │  56
└──────────────────────────────────────────────────────────────────────────┘
   204        204        204        204        204
```

- **Module size:** 204 × 544, four-pixel gutters. Five across 1024.
- **Signal order is left to right.** The leftmost module processes first. This is
  the same order as the chain's slots, so the mapping is identity.
- **The bottom 56 px is dead**, as everywhere else on this deck: the panel's
  lower edge sits under the chassis bezel and anything drawn there is invisible.
- **The `(+)` sits at the far right of the rack**, vertically centred, and
  scrolls with it — it is the end of the rack, not a fixed button.
- **Empty rack:** the `(+)` alone, centred, with `Add an effect` beneath it.

## 5. A module

```
┌─────────────────────┐  ← 2px bezel, module-specific
│ ◉                 ◉ │  ← screws, drawn into the chrome
│      ╭───────╮      │
│      │  ███  │      │  ← knob 1, largest: the metaknob
│      ╰───────╯      │
│        AMOUNT       │
│    ╭─────╮ ╭─────╮  │
│    │ ██  │ │  ██ │  │  ← knobs 2 and 3
│    ╰─────╯ ╰─────╯  │
│     DECAY    TONE   │
│                     │
│  ╭─────────────╮    │
│  │   REVERB    │    │  ← stylised name, bottom left
│  ╰─────────────╯    │
│ ◉              [◐]  │  ← bypass, bottom right
└─────────────────────┘
```

- **Name plate, bottom left**, in the module's own typeface treatment. Engraved
  into metal, silkscreened onto plastic — the name is part of the skin, not a
  label drawn over it.
- **Knobs:** one large metaknob at the top, then up to three parameter knobs.
  Every knob has a pointer, a value arc, and a caption underneath.
- **Bypass, bottom right**, with a lit/unlit state. Maps to the slot's `enabled`.
- **Screws are drawn, not images** — four per module, part of the chrome layer.

### Rendering

Each module's chrome — background texture, bezel, screws, name plate — is
**painted once into a `QPixmap` and cached**, keyed by module type and size. Only
knobs and the bypass lamp repaint on interaction, into that cached backdrop. The
deck is a Pi 4 with no compositor help for widgets; re-rendering a brushed-metal
gradient on every knob movement would be visible.

Knob repaint is confined to the knob's own rect.

## 6. Interaction

All touch. Every gesture below is a finger on glass.

| Gesture | Effect |
|---|---|
| **Drag a knob up/down** | Turn it. Vertical only — horizontal drag on a knob does nothing, so a sloppy diagonal still turns cleanly. |
| **Double-tap a knob** | Reset to the parameter's default. |
| **Tap the bypass** | Toggle the module in and out of circuit. |
| **Drag anywhere else** | Scroll the rack horizontally. No scrollbar, kinetic, clamped at both ends. |
| **Long-press a module** | Its border becomes a dashed outline: the module is now held. Drag left/right to reorder; release to drop. |
| **Tap `(+)`** | Open the effect chooser. |

**Knob sensitivity:** full travel over roughly 200 px of vertical drag, which is
about a third of the screen height — fine control without needing a modifier, and
still reachable in one movement. Values move in *parameter* space, so a detent of
drag is the same fraction of travel whatever the control's units.

**Reorder is a chain reorder**, not a visual one: dropping a module writes the
new slot order, and audio follows immediately.

**The encoder** keeps working while the rack is open: rotate scrolls the rack,
press does nothing. It is not the primary input here and is not worth
complicating the design for. BACK leaves the rack.

## 7. The effect chooser

Tapping `(+)` opens a chooser over the rack. It lists the available effects with
their module chrome in miniature — you pick the thing you are going to see, not a
row of text. Tapping one appends it to the right of the rack and closes.

Full when the chain is full: the `(+)` is dimmed and not tappable.

## 8. The effects, and their skins

Five to start. Each has a distinct material, and the materials are the point —
a DJ finds the filter by colour, not by reading.

| Module | Material | Engine |
|---|---|---|
| **Reverb** | Brushed metallic grey. Horizontal brush grain, slight vertical sheen gradient, darker at the edges. | `org.mixxx.effects.reverb` |
| **Delay** | Yellow plastic. Flat, slightly glossy, moulded. | `org.mixxx.effects.echo`, feedback low |
| **Echo** | Green plastic. Same treatment as Delay, different hue. | `org.mixxx.effects.echo`, feedback up |
| **HPF** | Slick shiny black. Piano-gloss, strong specular highlight. | `org.mixxx.effects.filter` |
| **LPF** | Slick shiny black. As HPF; distinguished by name plate and knob caption. | `org.mixxx.effects.filter` |

Knob styling follows the module: metal knobs on metal, plastic on plastic.

**Latency note:** the bus has a **32 ms** round trip (measured — see
`worklog/effect-pedal/measurements.md`). Reverb and Echo are unaffected; that
reads as pre-delay. Filters are *correlated* with the dry and will comb against
it with notches every 31 Hz. HPF and LPF on this bus will sound hollower than
they do in a mixer. This is inherent to a send/return and is not a bug to fix,
but it is a reason the filters may want to be the least-used modules.

## 9. Persistence

Rack contents, order, and every parameter live in `effects.xml`, which
`mixxx_config/upload.sh` already ships. This works correctly **provided effects
are loaded from their manifest** — a preset that carries an empty `<Parameters>`
list produces an effect that reports itself loaded with every parameter at zero,
and then re-serialises that same broken state on exit.

The bootstrap in `TriMixxx.setupPedalBus()` exists because of that trap and
should be removed once the rack writes correct presets of its own.

## 10. What moves to Diagnostics

Verbatim from the current effects page, appended as a new section:

- Aux input configured, aux to main, aux input level
- Unit routed, unit enabled, mix mode, effect loaded, effect on
- The slot group string

Plus, worth adding while we are there: the measured round-trip latency, and the
aux VU as a live bar. Diagnostics is already the page that reads `/proc` once a
second; this is the same kind of information.

## 11. Open questions

Numbered for answering.

1. **Per-module knob = metaknob?** Mixxx has one wet per *chain*. The proposal is
   that each module's big knob is that effect's metaknob, and there is one global
   wet for the rack. Accepted?
2. **Where does the global wet live?** Options: a fixed non-scrolling strip; a
   permanent leftmost "master" module; or nowhere on this screen, because the
   mixer's return fader already does it. Which?
3. **Raise `kNumEffectsPerUnit` above 4?** Scrolling only means something past
   five modules. What is the ceiling — 8, 16?
4. **HPF and LPF: two `Filter` instances, or new single-purpose builtins?** Two
   instances is zero engine work but each module hides two of its three knobs and
   two filters cost two slots. New builtins are cleaner and more code.
5. **Delay and Echo from the same `Echo` effect?** They would differ only in
   default feedback and skin. Acceptable, or should Delay be something else?
6. **Textures: procedural or PNG?** Procedural keeps the fork asset-free and
   scales to any module size; PNG gets exactly the look you want. Reference PNGs
   welcome either way — I can match them procedurally if you send them.
7. **Does this rack drive the pedal bus only?** It is `[EffectRack1_EffectUnit2]`
   today. Should the deck's own audio (`EffectUnit1` on `[Channel1]`) get a rack
   too, later, or is one rack the whole story?
8. **Per-module bypass — needed?** The metaknob at zero already silences most
   effects. A real bypass is more rack-like and gives tails that ring out.
9. **What happens on a full rack?** Dimmed `(+)`, or does adding replace the
   oldest?
10. **Is the 56 px bezel strip correct for this screen?** Taken from
    `browser-prd.md`; worth confirming it applies here.
