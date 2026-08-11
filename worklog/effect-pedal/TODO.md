# Effect pedal — implementation order

Turning TriMixxx into the effects unit on the Xone:92's AUX 1 send/return, so
that the CDJs' audio can be reverbed/delayed/crushed by the deck and returned
**wet only** — no dry, no level jump, one hand.

Wiring, mixer behaviour and why wet-only is non-negotiable:
`docs/xone92-send-return.md`.

Ordered from *most fundamental* to *most uncertain*. Each phase is a place where
the thing can be left working and put down. Don't start a phase before the one
above it is verified on the deck rather than on the laptop.

---

## Phase 0 — Prove the hardware path (no code)

**Software half: done, and it passes.** Measured on the deck 2026-08-11 with
Mixxx running.

The codec is a **TI PCM2902** (`08bb:2902`, "Burr-Brown from TI USB Audio
CODEC", full-speed USB, card 0). Findings:

- [x] **Stereo capture at 44.1 kHz exists** — `/proc/asound/card0/stream0`,
      Interface 2 Altset 3: `S16_LE`, 2 channels, 44100 Hz. Capture is a
      separate USB interface and endpoint (`0x84 IN`) from playback
      (`0x02 OUT`), so the two directions are independent.
- [x] **Full duplex confirmed.** `arecord -D hw:0,0 -c2 -f S16_LE -r44100 -d5`
      opened and ran to completion *while Mixxx held the playback stream*.
      Exit 0, file exactly 220500 frames = 5.000 s. No rate mismatch, no
      dropped frames. **This was risk #1 and it is cleared.**
- [x] **The ADC is alive and clean.** Nothing connected: noise floor
      −90.7 dBFS RMS, peak −78.3 dBFS, ~59% of samples non-zero. That is a real
      dither floor at the 16-bit theoretical limit, not a muted input.
      **Keep this number — it is the baseline for the hum test.**
- [x] **CPU headroom is ample.** Mixxx 18% of one core, load average 0.19 on
      four cores, zero xruns in the journal.
- [x] **16 bit only, and no capture gain control.** The card's only ALSA
      control is `PCM Playback Volume`; the PCM2902's ADC runs at fixed
      hardware gain. Input level is therefore set *entirely* by the Xone's send
      pot, with `[Auxiliary1] pregain` as the only trim after the fact.

**Deferred — needs the mixer physically connected:**

- [ ] Round-trip latency measurement. Loop the codec's output back to its own
      input, stop Mixxx, play an impulse and record simultaneously at the
      deck's buffer setting, cross-correlate. Assumed value below until then.
- [ ] Levels: find the send pot position giving peaks around −6 to −10 dBFS
      with a loud track. Expect it well below the centre detent — aux out is
      nominal −2 dBu into a consumer line input, before the pot's +6 dB.
- [x] **Hum: measured, and it's a non-issue.** Connected and powered with all
      sends off, the broadband floor rose 1.1 dB over the unconnected control
      (−90.7 → −89.6 dBFS). A mains component *is* present — 100 Hz up 8.7 dB,
      L/R correlation 0.246 → 0.400, the signature of common-mode rectifier
      ripple — but at −105.8 dBFS it sits 16 dB below the noise floor. No
      action. Full figures and method in `measurements.md`.
- [ ] Sustain: duplex for 20 minutes, watch the header's underrun counter. If
      it climbs, step `latency` to 4 and re-read the jog-bend cost noted in
      `mixxx_config/upload.sh`.
- [ ] **Thermals.** The SoC sat at **71 °C essentially idle** — warm for a Pi 4
      doing this little, and throttling starts around 80–85 °C. CPU headroom is
      not the constraint here; heat might be. Re-check under duplex + DSP in a
      closed chassis.

## Assumed values (unverified — replace with measurements)

Everything downstream is written against these. None of them block Phases 1–3;
they matter for Phase 4 and for final trim.

| Quantity | Assumed | Measured | |
|---|---|---|---|
| Round-trip latency | 20 ms | **32 ms ± 3** | Estimate was 60% low — both directions cost a full buffer, not a period, and the codec is USB 1.1 full speed |
| Send pot position | ~10–11 o'clock | **~1 o'clock → −12.8 dBFS peak** | Fully clockwise was −0.33 dBFS, no headroom at all |
| Noise floor once connected | ≤ −85 dBFS | **−89.6 dBFS** | 1.1 dB over the unconnected control; no action |
| `[Auxiliary1] pregain` | 1.0 (unity) | *unset* | Only trim available after the fixed-gain ADC |

Full method and figures in `measurements.md`. The latency being 32 rather than
20 ms tightens comb-filter notch spacing to 31 Hz (worse for transformative
effects, irrelevant to reverb and echo) and means Phase 4 cannot compensate
delay divisions shorter than 32 ms by subtraction — a 1/16 at 128 BPM is 29 ms,
shorter than the latency itself.

---

## Phase 1 — Aux input reaches the main mix, unprocessed

Pure config. Proves Mixxx's input side end to end before any engine work.

- [x] `mixxx_config/soundconfig.xml`: `<input channel="0" channel_count="2"
      index="0" type="Auxiliary"/>` inside the existing `<SoundDevice>`.
      `index="0"` is `[Auxiliary1]` — `PlayerManager::groupForAuxiliary()`
      returns `[Auxiliary{i+1}]`. Written before `<output>` to match the order
      Mixxx writes the file back in.
- [x] **`main_mix` cannot go in `mixxx.cfg`.** It is not a persisted control,
      and `EngineAux`'s constructor calls `setMainMix(false)` unconditionally
      on every start (`engineaux.cpp:24`), so a config value would be
      overwritten before anything reads it. Forced from `TriMixxx.init` in
      `TriMixxx.scripts.js` instead. **This is the failure mode that reads as
      "I configured the input and there is no sound".**
- [ ] Verify `[Auxiliary1] input_configured` reads 1 at runtime.
- [ ] Listen: the send should arrive in the main out clean and at sane level.
- [ ] Re-pull `soundconfig.xml` from the deck afterwards — Mixxx rewrites it at
      startup once devices are set up.
- [ ] Confirm behaviour when the codec re-enumerates (unplug/replug): does the
      aux come back, or does Mixxx need a restart?

**Exit criterion:** the Xone's aux bus is audible through TriMixxx's output.
This is also the moment the feedback loop first becomes possible — TriMixxx's
own channel send stays at zero from here on.

---

## Phase 2 — `WetOnly` chain mix mode (the core patch)

Mixxx cannot return wet-only today. `Reverb` and `Echo` declare
`setAddDryToWet(true)` and the chain re-adds the dry at
`engineeffectchain.cpp:268`; DRY/WET at mix=1 gives `wet + dry` and DRY+WET gives
`dry + wet×mix`. Both reproduce the Boss-pedal bug in software.

- [x] `src/effects/effectchainmixmode.h` — `WetOnly = 2`, `kNumModes = 3`
- [x] `src/effects/effectchainmixmode.cpp` — the `"WET"` string in
      `toString`/`fromString`
- [x] `engineeffectchain.cpp` — `skipAddingDry` now triggers for any mode that
      isn't `DrySlashWet`, not just `DryPlusWet`
- [x] `engineeffectchain.cpp` — third branch: `out = wet × mix` via
      `copyWithRampingGain` (single source), so the wet still ramps and doesn't
      click on a fast mix move
- [x] **Empty-chain silence guard** (blocker 1 below) — handled after the
      processing block, gated on `channelStatus.enableState != Disabled` so it
      silences a routed-but-idle chain without touching channels the unit isn't
      routed to
- [ ] **Build it.** Not yet compiled — needs the arm64 Docker build. Nothing
      below Phase 2 has run on hardware.
- [ ] Extend the `mixxx-test` target if there's an existing engineeffectchain
      test; otherwise assert the three modes by hand with a known buffer

### Blockers and things to check, in this phase

1. ~~**The empty-chain dry leak.**~~ **Handled, needs verifying by ear.**
   `EngineEffectChain::process()` only writes `pOut` when `processingOccured`
   and returns false otherwise, which would leave the caller holding the
   unprocessed input — the +6 dB doubling bug, armed by clearing an effect slot
   mid-set. Now guarded: in WetOnly, a routed chain that processed nothing
   clears `pOut` and reports true. Three cases to check on hardware: **(a)**
   clear the last effect slot → silence, not dry; **(b)** switch the unit off
   with `enabled` → silence, not dry; **(c)** set `group_[Auxiliary1]_enable 0`
   → dry passes again, because the unit is no longer on that channel at all.
   (c) is the one most likely to be wrong, since it depends on `Enabling` being
   the settled state for a routed-but-off channel.
2. ~~**Three-state toggle side effects.**~~ **Checked, no impact.**
   `mix_mode` is a `ControlPushButton` in TOGGLE mode sized by
   `setStates(kNumModes)` (`effectchain.cpp:86`), so 3 states changes the cycle
   for every chain type. Nothing in `mixxx_config/` references `mix_mode` at
   all — only the stock skins do, and the deck doesn't load them.
3. **`effects.xml` is not forward-compatible.** `fromString` falls back to
   `DrySlashWet` on an unrecognised string, so a preset written by the patched
   build and read by a stock Mixxx silently becomes DRY/WET — full dry into the
   mix on top of the mixer's dry. Don't let a stock binary open the deck's
   `~/.mixxx/effects.xml`.
4. **Upstreamability.** This is a genuinely general fix (every send/return user
   on every mixer has this problem). Keep the patch clean enough to offer
   upstream rather than carrying it forever.

---

## Phase 3 — Route a unit to the aux and make it sound right

- [ ] Dedicate one unit (EffectUnit2 suggested — Unit1 is already routed to
      `[Channel1]`): `group_[Auxiliary1]_enable 1`, `group_[Channel1]_enable 0`,
      `mix_mode 2`
- [ ] Chain `Filter (HPF) → Reverb`. HPF ~200–300 Hz on the wet keeps the effect
      obvious while removing the low-mid energy that carries perceived loudness,
      so the "no volume change" goal survives contact with a real system.
- [ ] Establish the two gestures and label them clearly:
      - `[Auxiliary1] pregain` = **send** — pre-effect, so pulling it stops
        feeding the tank and the existing tail rings out
      - `[EffectRack1_EffectUnit2] mix` = **return** — post-effect, cuts the
        tail dead
- [ ] Ship the preset: chains live in `~/.mixxx/effects.xml`
      (`effectsmanager.cpp:21`). Add it to `upload.sh` with the same
      stop-Mixxx-first handling as `mixxx.cfg`, and the same XML pre-validation.
- [ ] Sanity test at the mixer: send at zero → return is silent; send up →
      reverb only, dry level unchanged. **If the dry level moves at all, stop**
      — something is still passing dry.

**Exit criterion:** the original problem is solved. Everything below is
refinement.

---

## Phase 4 — Beat sync on the aux bus

Echo's `Quantize` and `Triplets` are dead on an aux: `echoeffect.cpp:135` needs
`groupFeatures.beat_length`, `EngineAux::collectFeatures` supplies only the VU
meter, so the Time knob falls through to the seconds branch at `:148`.

- [ ] Decide the tempo source: `[Channel1]`'s own beatgrid, or the ProLink
      network tempo master (which this deck already tracks — see
      `docs/tempo-sync.md`)
- [ ] Inject `beat_length` into the aux's `GroupFeatureState`
- [ ] Subtract the Phase 0 round-trip latency from the delay time so the taps
      land on the grid rather than ~20 ms behind it

**Uncertain, and the reason this is Phase 4 not Phase 2:** tempo is the easy
half. A delay wants to know *where the beat is*, not just how long it is, and
`beat_fraction` for a live input mixed from several CDJs has no single correct
answer. Possibly the honest scope is "quantise the delay time, don't try to
phase-align", which is still a large improvement over seconds.

---

## Phase 5 — The FX tab

- [ ] Third child of `RootStack` in `skin.xml`, alongside `DeckView` and
      `LibraryView`
- [ ] Widgets exist already (`legacyskinparser.cpp:602-619`): `EffectSelector`,
      `EffectName`, `EffectMetaKnob`, `EffectParameterKnob`,
      `EffectParameterName`, `EffectPushButton`, `EffectChainPresetButton`
- [ ] Prefer the slot controls over dropdowns for touch: `next_effect`,
      `prev_effect`, `loaded_effect`, `effect_selector`, `enabled`, `meta`,
      `parameterN` (`effectslot.cpp:78-123`)
- [ ] **Open decision — the control surface.** Ring A and B are fully
      allocated, so a live wet knob has to come from somewhere: the touchscreen,
      the browse encoder over the deck view (currently waveform zoom, a
      set-and-forget binding), or repurposing a pad as a momentary throw. With
      wet-only working, the mixer's own send pots are the natural performance
      control and the deck side may only need a trim — decide before building
      the panel, not after.
- [ ] Chain presets per genre, switchable live

---

## Phase 6 — LV2 host upgrade

Mixxx's LV2 backend calls `lilv_plugin_instantiate(pPlugin, sampleRate, nullptr)`
(`lv2effectprocessor.h:28`) — zero host features — so `lv2manifest.cpp:183`
correctly rejects every plugin declaring a required feature, and `:177` rejects
anything that isn't exactly 2-in/2-out. `liblilv-dev` is already a build dep
(`Dockerfile:54`), so the backend is compiled in and this is purely about what it
will accept.

- [ ] Survey first: script on the Pi that walks the installed LV2 bundles and
      reports, per plugin, audio port counts and required features — i.e. what
      Mixxx would accept today. Cheap, and it decides whether the rest is worth
      doing.
- [ ] `urid:map` / `urid:unmap` — URI↔uint32 table. Smallest change with the
      biggest unlock.
- [ ] `options` + `buf-size` — advertise sample rate and max block length
- [ ] Channel adaptation — accept mono and 1-in/2-out plugins (most classic
      reverbs are 1-in/2-out)
- [ ] Atom sequence ports — lets plugins receive host tempo, i.e. plugin delays
      that beat-sync
- [ ] `worker`/`schedule` — unlocks convolution reverb
- [ ] `state` — plugin presets

---

## Phase 7 — Polish

- [ ] Safety limiter on the wet return (the send pot has +6 dB and a feedback-y
      delay can run away)
- [ ] LED / on-screen indication that the pedal bus is live
- [ ] Document the mixer setup on the deck itself, or in the README

---

## Risk register

Ordered by how much is lost if the answer is bad.

| # | Risk | Why it matters | When we'll know |
|---|---|---|---|
| ~~1~~ | ~~Codec isn't full-duplex~~ | **Cleared** — PCM2902 does stereo 44.1 k capture concurrently with playback | Phase 0, done |
| 2 | Duplex xruns at the current buffer | Forces higher latency, which worsens comb filtering and jog bend | Deferred — needs sustained duplex |
| 2b | **Thermal, not CPU** | 71 °C idle, throttles ~80 °C, closed chassis. CPU is at 18% and irrelevant | Under load, deferred |
| 3 | Empty-chain dry leak | Silent trap that reproduces the exact bug we're fixing, mid-set | **Guarded in Phase 2; unverified** |
| 4 | Feedback loop via TriMixxx's own aux send | Howl-around in front of a crowd; mixer-side discipline only | Phase 1 onward |
| ~~5~~ | ~~CPU headroom on the Pi 4~~ | **Cleared** — 18% of one core, load 0.19, no xruns | Phase 0, done |
| 5b | 16-bit fixed-gain ADC, no input trim | Send pot is the *only* input gain control; clipping has no software rescue | Level test, deferred |
| 6 | Comb filtering on transformative effects (crush, filter, pitch) | Their output stays correlated with the dry, and the return is ~20 ms late; reverb and echo are immune | Phase 3, by ear |
| 7 | `effects.xml` read by a stock Mixxx build | Silent downgrade to DRY/WET = full dry into the mix | Phase 2 — note it |
| 8 | Beat phase on a multi-CDJ live input may have no correct answer | Limits Phase 4 to tempo-only quantise | Phase 4 |
| 9 | LV2 ecosystem may not repay the host work | Survey before committing | Phase 6, step 1 |

---

## Open questions

- Which mixer channel does TriMixxx return on? A music channel (CH 1-4) gets the
  VCF and crossfader, which the deck wants as a player; `RTN 1/2` does not.
- Does the deck's own track ever want the pedal bus? It can't — that closes the
  feedback loop. Its effects have to be internal (a second unit on
  `[Channel1]`), which means two FX controls unless the tab hides the seam.
- Is a second cheap USB DAC worth it later? It would give the wet return its own
  mixer channel and break the coupling where fading TriMixxx out also pulls the
  effect off every other channel.
