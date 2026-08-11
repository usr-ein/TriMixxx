# The Xone:92's send and return, and what it does about wet/dry

Reference for the effect-pedal work: what the mixer actually gives us, which
knob does what to the send, and why the "I hear the track twice" problem is not
something the mixer can be talked out of.

Source is `datasheet/Xone92-Mk2-User-Guide.pdf` (Issue 1, © 2024 Allen & Heath),
text plus the block diagram on page 26. Claims below are from the manual unless
marked otherwise; the ones read off the block diagram rather than stated in
prose are called out, because a schematic squinted at is not the same as a
sentence.

> **Mk2 caveat.** This is the Mk2 guide. The aux architecture is the same
> topology on the original AP5345 machine, but the Mk2 adds soft-switched filter
> assign, MIC clean-feed switches and the innoFADER Pro. If your unit is the
> original, treat the *numbers* here as Mk2 numbers and the *routing* as shared.

## There is no FX loop — there are two aux buses

The Xone:92 has no dedicated effects send/return section. What it has is **AUX 1
and AUX 2**, two conventional stereo aux buses, and you build a send/return out
of them. Per the specification page:

| | |
|---|---|
| Aux sends | 2 × on each of CH 1-4, MIC/RTN 1, MIC/RTN 2 — selectable pre/post |
| Aux outputs | 2 × ¼" TRS stereo, impedance balanced, **nominal −2 dBu** |
| Returns | 2 × ¼" TRS `RTN 1`/`RTN 2` (mono into L/M, or stereo), or any channel's line input |

Two details that matter for gain staging. The send pot runs from **off** (fully
anticlockwise) to **+6 dB** (fully clockwise), so unity is somewhere around
three-quarters and there is real gain on tap. And there is **no aux master
level** — the block diagram takes `AUX 1 MIX` straight to the output jacks
through nothing but the impedance-balancing resistors. The only things between a
channel and the send output are the channel's own controls and that one pot, so
the receiving device's input trim is doing all the remaining gain staging.

## What is in the send path, and what isn't

This is the part the manual's prose leaves out and the block diagram answers.
The stereo channel strip runs:

```
PHONO/LINE → LEVEL → 4-BAND EQ →─┬─────────────────────→ [pre tap]
                                 │
                                 VCA ──┬───────────────→ [post tap]
                              (fader ×  │
                           crossfader × │
                            upfade curve)
                                        └→ FILTER SELECT → filter bus → LR mix
```

Both aux taps come off **before** the `FILTER SELECT` block. The consequences:

| Control | In the send? | Note |
|---|---|---|
| `LEVEL` (input gain) | **Always** | It's the first thing in the chain |
| 4-band EQ | **Always** | Both taps are post-EQ — killing a channel's bass also removes it from what the effect hears |
| Channel fader | Only post-fade | |
| Crossfader | Only post-fade, and only on CH 1-4 | MIC/RTN channels don't reach the crossfader |
| **Xone:VCF filter** | **Never** | *(read off the block diagram, not stated in the text)* |
| `CUE` | No | Monitor only |

The filter one is worth internalising: you can sweep a channel's VCF all the way
closed and the reverb send keeps receiving the full-bandwidth signal, so the tail
carries on unfiltered. Whether that's a feature or a nuisance depends on the
transition, but it's not adjustable — the tap point is fixed in hardware.

The EQ one is the reverse and equally fixed: there is no way to send a channel
to the effect *pre*-EQ.

## The PRE switches

Each channel has **two** PRE switches, one per aux bus, sitting under the two
send pots. Up is post-fade, pressed is pre-fade. That's the only control on the
mixer that changes the *character* of the send rather than its level.

**Post-fade** (switch up) is the default and the right one here. The manual's own
justification is exactly the property we want:

> Post-fade sends are typically used to send channel signals to effects devices
> such as reverb or delay processors. The amount of signal sent to the device
> follows the fader level. The processed (wet) signal returned to the mix
> elsewhere is therefore **in proportion to the direct (dry) signal regardless of
> fader position.**

So with post-fade, the wet/dry ratio is a constant that you set once with the
send pot, and it survives every fader and crossfader move. Fade a CDJ in and its
reverb fades in with it, in proportion.

**Pre-fade** (switch pressed) decouples them: the effect keeps being fed at full
level no matter what the fader does. That's the "throw" — drop the channel fader
to zero and the track vanishes while its reverb tail carries on alone over the
next record. Musically excellent, but it is a different gesture, not a better
default, and with pre-fade set you can no longer stop feeding the effect without
reaching for the send pot.

## The wet/dry answer: the mixer is purely additive, and has no blend anywhere

There is no wet/dry control on the Xone:92. Not on the channel, not on the aux,
not in the master section. The main mix is a plain sum:

```
MIX = Σ(channel dry contributions) + Σ(return channel contributions)
```

Bringing a return up **never attenuates the dry**. The mixer does not know, and
cannot be told, that the return is related to a channel it is already carrying.
In the vocabulary of the effects world it implements *dry + wet*, always, and it
has no *dry/wet blend* mode to offer.

Which is where the problem comes from, and it is not the mixer's fault:

- A Boss compact through INPUT A returns **dry + wet**.
- The mixer adds that to the channel's own dry.
- Result: `2 × dry + wet`. The two dry copies are coherent (the pedal's input-A
  path is analogue), so they sum to **exactly +6 dB** on the track.

The only way to fix this is at the device, because the mixer has no subtraction
to offer. Either the device outputs wet only, or you spend two hands pulling the
channel fader and the return fader down together to claw the 6 dB back — which
is precisely the manoeuvre we are trying to eliminate.

The "blend" you get on a Xone is therefore only ever **the ratio between the
source channel's fader and the return channel's fader**, and it is only a
*balance*, never a *crossfade*: nothing you do to the return fader takes the dry
away.

## The return side

Two choices, and they are not equivalent.

**`RTN 1` / `RTN 2` on the MIC/RTN channels.** Purpose-built for this. Press the
`MIC/RTN` switch to select the line return (the LED goes green → red). You get
`LEVEL`, a 4-band EQ (±15 dB at 12 k / 2.7 k / 270 / 60 Hz — a corrective EQ, not
the music channels' asymmetric one) and a fader. You do **not** get the
crossfader or the VCF filter.

**A music channel (CH 1-4) line input.** Costs you a deck's channel, but you get
the asymmetric performance EQ with infinite LO/HI kill, the VCF assign and the
crossfader.

For TriMixxx the second is the one, because the deck's output and the wet return
share that jack — TriMixxx is a player *and* the return, so it wants a player's
channel with a filter on it.

## Rules for our wiring

`AUX 1 OUT → TriMixxx input`, `TriMixxx output → CH n line in`.

1. **The send pot on TriMixxx's own channel stays at zero.** TriMixxx's output
   carries the wet return, so any send from that channel closes a loop:
   out → channel → aux → in → effect → out, with the deck's ~20 ms round trip as
   the loop delay. It will howl once the loop gain passes unity. (Deliberately
   creeping it up is a known Xone technique, but it is a stunt, not a setting.)
2. **All other channels post-fade** unless you specifically want a throw.
3. **Set the send level with the aux monitor.** The `AUX 1`/`AUX 2` switches in
   the master section put the aux bus in the headphones — that is how to hear
   what TriMixxx is actually being fed, without it being in the house mix.
4. **Gain staging**: aux out is nominal −2 dBu into whatever the USB codec's
   line input expects. Check for clipping at the codec, not at the mixer, since
   the send pot has +6 dB above unity and nothing downstream of it on the mixer
   limits.
5. **TriMixxx's channel fader is a master over both** its own deck and the global
   wet return. Fade the deck out and the effect goes with it. That is the price
   of one output and there is no mixer-side fix.

## Consequence for the software

The mixer contributes the dry. TriMixxx must therefore return **wet only** — not
"mostly wet", not "dry+wet with the dry turned down". Any dry that comes back
lands on top of a dry the mixer is already carrying, and the error is a level
error that no amount of return-fader riding can undo without also changing the
overall volume.

That requirement is what drives the `WetOnly` chain mix mode in
`worklog/effect-pedal/TODO.md`: Mixxx's stock DRY/WET and DRY+WET modes both
re-add the dry, so both would reproduce the pedal's bug in software.
