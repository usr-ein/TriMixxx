# Effect pedal — implementation order

Turning TriMixxx into the effects unit on the Xone:92's AUX 1 send/return, so
that the CDJs' audio can be reverbed/delayed/crushed by the deck and returned
**wet only** — no dry, no level jump, one hand.

Wiring, mixer behaviour and why wet-only is non-negotiable:
`docs/xone92-send-return.md`.

Ordered from *most fundamental* to *most uncertain*. Each phase is a place where
the thing can be left working and put down. Don't start a phase before the one
above it is verified on the deck rather than on the laptop.

## Where this is

| Phase | State | |
|---|---|---|
| 0 · Hardware path | **done** | PCM2902 does full duplex at 44.1 k. 32 ms round trip, −12.8 dBFS in, no hum. Two soak tests outstanding. |
| 1 · Aux reaches the mix | **done** | |
| 2 · `WetOnly` mix mode | **done** | Plus six slots, per-slot wet, and the `kNumModes` leak fixed |
| 3 · A unit on the aux | **done** | Wet-only reverb on the CDJs, surviving a reboot |
| 4 · Beat sync | **done** | Oldest playing deck wins. Latency compensation not attempted |
| 5 · The rack | **built, and tested once** | Superseded by `docs/effects-prd.md`; the test run's list is Phase 8 |
| 6 · LV2 host | not started | Survey first — it decides whether the rest is worth doing |
| 7 · Polish | not started | |
| **8 · The test run's list** | **done bar one** | Only §15.1, the deck into the send bus, is left |
| **9 · The second test run's list** | **done** | §17.1–17.4; 17.3 was a closed filter, not the master |
| 10 · The FX strip (§16) | **done** | Built and verified; focus has no timeout |

**Next action:** a test run. Every PRD item is now built. Everything below is on the deck and verified as far
as it can be without ears — the two things that need a listener are the master
rocker (does RING OUT actually ring out?) and the delay divisions at tempo.

After that, the two remaining PRD items in order: **§15.1**, the deck's own
audio into the send bus with an INPUT module, then **§16**, the FX strip on the
deck view. §16 was blocked on metering and the mute-mode toggle; both now
exist.

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

- [x] **Round-trip latency: 32 ms ± 3.** `alsabat --roundtriplatency` at the
      deck's own buffer settings, three runs. 60% above the 20 ms estimate,
      because both directions cost a full buffer rather than a period and the
      codec is USB 1.1 full speed. Method and consequences in
      `measurements.md`.
- [x] **Levels: settled at ~1 o'clock, −12.8 dBFS peak.** Fully clockwise was
      −0.33 dBFS, no headroom at all. −12.8 looks conservative for one source
      and is not: the aux bus sums, so two channels at this level already peak
      near −7.
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

## Assumed values, and what they turned out to be

All measured now. Kept side by side because two of the estimates were wrong in
ways that mattered.

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
- [x] Verified: `Input channels: 2`, `1 input sound devices opened`, capture
      stream `Running`, and the Effects page reports the aux configured and on
      the main mix.
- [x] Listened. The send arrives clean.
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
- [x] **Built, deployed and audible.** Also since extended for the rack: six
      slots per unit, a per-slot `wet` control blended in the chain loop
      (WetOnly chains only), that value carried in `EffectPreset`, and the
      `mix_mode` toggle put back to two states so `WET` stops leaking into
      QuickEffect and Equalizer chains.
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

- [x] EffectUnit2 dedicated to `[Auxiliary1]`, `mix_mode` WET.
- [ ] Chain `Filter (HPF) → Reverb`. HPF ~200–300 Hz on the wet keeps the effect
      obvious while removing the low-mid energy that carries perceived loudness,
      so the "no volume change" goal survives contact with a real system.
- [ ] Establish the two gestures and label them clearly:
      - `[Auxiliary1] pregain` = **send** — pre-effect, so pulling it stops
        feeding the tank and the existing tail rings out
      - `[EffectRack1_EffectUnit2] mix` = **return** — post-effect, cuts the
        tail dead
- [x] Preset shipped: `mixxx_config/effects.xml`, validated and copied by
      `upload.sh` with the same stop-first handling as `mixxx.cfg`.
- [x] Sanity tested at the mixer: reverb only, dry level unchanged, and it
      survives a reboot with no manual step.

**Exit criterion:** the original problem is solved. Everything below is
refinement.

### Done — and what actually bit us

Working on the deck, audible on the CDJ, surviving a reboot with no manual step.
None of the four things that had to be true were in the plan, and all four fail
*silently*, which is why the Effects page states each of them outright:

1. **`main_mix` cannot be configured.** Not a persisted control, and
   `EngineAux`'s constructor calls `setMainMix(false)` on every start, so
   `mixxx.cfg` is powerless. Forced from `TriMixxx.setupPedalBus()`.
2. **`StandardEffectChain` does not enable its slots.** It is the only chain
   type that doesn't — Output, QuickEffect and Equalizer all call
   `setEnabled(true)`. Upstream expects a skin to draw an enable button. Ours
   draws none, and `EffectPreset` has no `enabled` field to carry it either.
3. **An empty `<Parameters>` list is worse than useless, and self-perpetuating.**
   `loadEffectInner` clears `m_loadedParameters` then refills it *from the
   preset*, so a hand-written preset with `<Parameters/>` maps nothing — leaving
   an effect that reports `loaded = 1` with every parameter at 0. It then
   serialises an empty list again on exit, because `EffectPreset` reads
   `m_loadedParameters`. **The bad state cannot be fixed by capturing what the
   deck writes**; the load has to come from the manifest, via `loaded_effect` →
   `loadEffectWithDefaults`.
4. **`send_amount` defaults to 0** and is Linked to the *effect slot's*
   metaknob — not the chain's `super1`, which is a different control. Nothing
   drives it on its own, so a correctly loaded reverb is still silent.

Plus an ordering race worth remembering: the controller opens ~30 ms *before*
SoundManager sets up devices, so `init` runs while `[Auxiliary1]` has no input.
Across restarts, `main_mix` and `loaded` were observed flipping in opposite
directions. The setup is now idempotent and asserted three times — at init, on
`input_configured`, and on a 2 s timer — with the ready flag set from what the
control *reads back* rather than from having made the call.

**Still to do here:** the `Filter (HPF) → Reverb` chain, so the wet is
high-passed and the perceived level holds steadier.

---

## Phase 4 — Beat sync on the aux bus

Echo's `Quantize` and `Triplets` are dead on an aux: `echoeffect.cpp:135` needs
`groupFeatures.beat_length`, `EngineAux::collectFeatures` supplies only the VU
meter, so the Time knob falls through to the seconds branch at `:148`.

- [x] **Tempo source: whichever deck has been playing longest.** Not the network
      master — a master can be a deck sitting cued. Candidates are the Pro DJ
      Link players judged by `isWorthFollowing()` (the same test the phase meter
      uses, which already excludes cued, paused, searching and spun-down decks)
      and this deck's own player on the same terms. Oldest wins and keeps
      winning until it stops; entries drop the moment a deck stops, so a deck
      that pauses and restarts is correctly the youngest again. Published as
      `[EffectTempo] bpm` and `source`.
- [x] **`beat_length` injected** in `EngineAux::collectFeatures`.
- [x] **The name bar says which deck and at what tempo**, because a delay
      quantised to the wrong deck sounds exactly like a broken one. Also logged
      on change, which is how it was verified without a screen.
- [ ] **Latency compensation is not done.** The taps sit ~32 ms late. Divisions
      shorter than that cannot be compensated by subtraction at all — a 1/16 at
      128 BPM is 29 ms — so this needs wrapping to the next beat rather than
      subtracting, and has not been attempted.

**The uncertainty resolved the way the phase predicted.** Tempo was the easy
half; phase was not attempted. `beat_fraction_buffer_end` is deliberately left
unset: the phase of several decks summed by a mixer and arriving 32 ms late has
no single right answer, so none is given. Quantising the delay *length* to a
musical division is the useful part and that is what shipped.

---

## Phase 5 — The effect rack  *(superseded: see `docs/effects-prd.md`)*

The "FX tab" sketched here became a full design and a signed-off PRD, revision
3. Everything below is now specified there rather than guessed at: a horizontal
rack of drawn modules, per-unit wet with the first dry-killer locked open, a
master module pinned right and driven by the encoder, drag to scroll, hold to
reorder, drag-to-bin to remove, and racks saved as stock Mixxx chain presets.

**The engine work it rests on is done** (Phase 2). What remains is UI:

- [ ] **Move the debug status block to Diagnostics.** Aux configured, aux to
      main, aux level, unit routed, unit enabled, mix mode, effect loaded,
      effect on, slot group — plus the measured 32 ms round trip and the aux VU
      as a live bar. It is diagnostic information and belongs with the rest of
      it. The rack shows no status text.
- [ ] **Invert the browser's gesture dispatch.** `move`, `select` and `back`
      each carry a chain of `if (m_stack.last().kind == …)` — sort menu,
      diagnostics, effects — growing by three every time a page is added. The
      rack needs drag, long-press and horizontal scroll on top. A page should
      claim the gestures it wants instead of the browser knowing about each one.
- [ ] **Replace `WDeckEffects` wholesale.** It rebuilds a rich-text document
      through `setHtml` five times a second; it cannot do dragged knobs and
      cannot do drag-and-drop reordering at all. It was scaffolding and says so.
- [ ] Module chrome: cached `QPixmap` per module type, procedural, from the
      palettes sampled off the reference skins (PRD §9).
- [ ] Knobs, drag and double-tap-to-reset.
- [ ] Rack scroll, long-press reorder, bin.
- [ ] The `(+)` chooser.
- [ ] Master module + encoder + mute.
- [ ] Name bar and the rack browser.

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

## Phase 8 — the first test run's list

From a real run with the rack in front of a mixer. Full reasoning per item in
`docs/effects-prd.md` §15; this is the order to do them in.

**Ordered by what is blocking use.** The first two make the rack behave; the
engine work after them makes it good.

- [x] **The rack empties when you leave the page.** `writeChainToEngine()` only
      writes `loaded_effect` when `module.slot != i`, and a module appended by
      the chooser already has `slot == i` — so it is drawn but never loaded, and
      `syncFromEngine()` on return finds an empty chain. One condition. **Do
      this first; it makes everything else testable.** (§15.5)
- [x] **Filters lose their wet knob.** `out = filter(in, cutoff)` and nothing
      else — the blend double-counts the passband and combs it against itself.
      Cutoff becomes a frequency in Hz, HPF cuts below and LPF above, both
      default mid-band. (§15.6)
- [x] **Saving must not ask for a name.** `savePreset()` opens a dialog; the
      deck has no keyboard. Generate the name and write the file. Then: saved
      racks appear in the list, the list scrolls, and the last one loads at
      startup. (§15.5)
- [x] **Drag keeps its grab point** rather than centring the module on the
      finger. (§15.7)
- [x] **Reorder displaces live** — neighbours slide out of the way as the held
      module crosses them, so the rack always shows the order that would
      result. (§15.8)
- [x] **Delay and Echo time in fixed divisions**, shown beside the dial:
      1/16 · 1/8 · 1/4 · 1/2 · 3/4 · 1 · 2 · 4 · 8. (§15.9)
- [x] **Makeup gain on the wet.** The chain outputs `wet × mix` with `mix`
      maxing at unity, and a reverb tail at unity is far quieter than the dry
      beside it. The master wants range above 0 dB. (§15.4)
- [x] **Master ring-out toggle.** A rocker choosing whether the master scales
      the chain's input (tails ring out) or its output (cut dead). Default ring
      out. (§15.2)

### Phase 9 — the second test run's list — **done**

Reasoning in `docs/effects-prd.md` §17.

- [x] **The rocker glitched on the way across.** It parked the stage it was
      leaving at unity *before* handing the level to the one it was arriving at,
      so for one buffer both were open and the chain ran at full wet. Reversed,
      which turns a burst into an inaudible dip.
- [x] **Mute did not survive the rocker.** Flipping while muted carried the
      zero across and left the real level stranded in `m_mutedLevel`.
- [x] **CUT "did not work at all" — it did.** An LPF three modules in was at its
      13 Hz floor and everything downstream was reverberating silence. Measured:
      −79.6 dBFS with it closed, −37.9 with it open. §15.3 metering is the fix
      for the class, and it now shows exactly where a chain dies.
- [x] **The encoder follows the last knob touched**, with a lit ring saying
      which. Press still always mutes the master.

### Found while testing, not on any list

- [x] **Mixxx segfaulted on every shutdown**, so *nothing* persisted — not the
      rack, not mixxx.cfg. One cause fixed (a destructor emitting signals back
      into the object destroying it); a second remains, recorded below.
- [x] **The rack persists itself now**, two seconds after each change, rather
      than depending on a clean exit that a booth deck rarely gets anyway.
- [x] **`deck-record`** joins `deck-shot` and `deck-poke`: one sees, one
      touches, this one listens. It records Mixxx's own main mix, which is the
      only way to hear the aux return and the chain from here.
- [x] **The build was capped and pinned.** Trixie pinned rather than read off
      the deck, build trees keyed per release, parallelism capped so a cold
      build stops OOMing.

### The FX strip on the deck view (§16)

Reaching the effects should not mean leaving the waveform. **Depends on the two
engine items below** — without metering the VU has nothing to draw, and without
the mute-mode toggle the rocker has nothing to switch — so it comes after them.

- [ ] An **FX** section down the left of the waveform on the deck view: VU,
      master knob, mute-mode rocker.
- [ ] **Touch claims the encoder** — rotate is the FX master, press is its mute,
      and the border lights while it holds focus. Touching anything else
      releases it and the encoder returns to library/zoom. The deck view needs
      its own version of the `DeckPage` claim mechanism, or the two want
      merging.
- [ ] **Decide the focus timeout** (§16). Focus takes the encoder press away
      from the library while held; a timeout gives it back on its own, no
      timeout keeps it where it was put. Better mid-transition either way is not
      obvious.
- [ ] **Factor the chrome out of `WDeckRack`** — bevel, knob, VU, engraved text
      — now that there is a second caller. Doing it earlier would have been
      guessing at what the second caller needed.

### Engine work in this phase

- [ ] **The rack must reach the deck's own audio.** Today it can effect every
      channel on the mixer except the one the deck is playing. Routing the unit
      to both channels does *not* do it — Mixxx units are per-channel and would
      process them separately, taking the deck's dry with them. Proposed: sum a
      scaled copy of the deck's post-fader audio into the aux buffer inside
      `EngineAux::process()`, before the chain, so the aux channel *becomes* the
      send bus and everything downstream is unchanged. **Check `EngineMixer`'s
      channel order first** — the deck must be processed before the aux, and the
      wrong order is a buffer one callback late, which is inaudible and still
      wrong. Two send dials in a fixed INPUT module at the far left, mirroring
      the master at the far right. (§15.1)
- [ ] **Per-slot output metering.** Nothing publishes an effect's output level;
      it has to be measured in `EngineEffectChain::process()` after each effect
      and published as a control. Feeds a VU on every module and on the master,
      drawn as a segmented ladder rather than a bar. (§15.3)

---

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

---

## Open: Mixxx still segfaults at the end of shutdown

**Not the effects, but it was hiding as them.** Mixxx crashed on every
shutdown from 12 August 19:04 until 13 August, which is why the rack seemed
not to persist -- and why *nothing* persisted, mixxx.cfg included, since the
crash lands before `CoreServices::finalize()` saves anything.

One cause found and fixed: `~MediaRegistry` -> `~ProLinkNetworkService` ->
`shutdown()` -> `MediaRegistry::onDeviceLost()`, a destructor emitting a signal
back into the half-destroyed object that owns it, which ran a database query
and aborted inside malloc. See the commit for the symbolised trace.

**A second crash remains**, and it is further along:

```
  debug [Main] 87 ms deleting menubar
  Segmentation fault        (sometimes Bus error -- it alternates)
```

What is known:

- It is at `mixxxmainwindow.cpp:520`, `m_pMenuBar = nullptr` /
  `m_pMenuBar.toWeakRef()`, crashing inside
  `QtSharedPointer::ExternalRefCountData::getAndRef` -- so `m_pMenuBar` is a
  non-null dangling pointer by then. That is stock Mixxx code.
- **Not our skin.** It crashes identically with the stock Deere skin.
- **Not testable against stock Mixxx**: the apt binary exits 143 on SIGTERM
  because handling that signal is our patch, so it never enters the graceful
  path at all.
- The crash type alternating between SIGSEGV and SIGBUS at the same line says
  the heap is already corrupt before it gets there, so the cause is upstream of
  the symptom.

Persistence no longer depends on this being fixed -- the rack saves itself two
seconds after each change -- so this is a tidiness and settings-durability
issue rather than a data-loss one. **Next step if picked up:** bisect between
`94d2af3` (12 Aug 09:03, the last build known to shut down cleanly) and
`3e8022b`, or run one boot with `MALLOC_CHECK_=3` to abort at the corrupting
operation rather than at its victim.

## Open: an unreproduced bad restore

Once, a cutoff saved as 2658 Hz came back as 13 Hz after a restart. Not
reproducible since -- 4335 Hz saved and restored correctly on the same build --
and the file was verified to hold the right value before the restart. Recorded
because eager persistence makes any transient bad read *permanent*: it would
overwrite the good file two seconds later. That is why `persistSoon()` ignores
the startup read.


## Open: playlists whose tracks only exist in Device Library Plus

Reported 14 August, in the user's words:

> There is a problem loading playlists inside folders. In my usb, the folder
> "techno night" has playlists, but they all appear completely empty while they
> are not. The other folder, which is a date like "2027-04-02" has also
> playlists, and those work. It may have to do with the fact there is a space in
> the folder name?

**Not the space.** `Sssssh claps` (13 tracks), `breakcore start` (10) and
`alba franch ` (6, trailing space) all work.

**The tracks are not in `export.pdb`, and that is not us dropping them.**
Verified on the stick itself, mounted directly, with two probes now in
lib/prolink:

- `pl_probe` — the four playlists under `techno night` (rb_id 25–28) have zero
  entry rows, while the six under `2025-06-29` have 1, 2, 1, 3, 9 and 106. Not
  one of the file's 1200 entry rows is orphaned.
- `all_pages` and `entries_pages` — the `playlist_entries` table has 6 pages
  declaring 1195 rows, and the chain walk visits all 6 and reads 1200 (the
  presence mask finds 5 the header does not count, which is the documented F47
  behaviour). **We read every playlist-entries page in the file.**

**Where the tracks actually are: `exportLibrary.db`.** That stick carries a
rekordbox 6 *Device Library Plus* library beside the classic one —
`exportLibrary.db` (544 kB, SQLCipher-encrypted, not plain SQLite),
`exportExt.pdb`, and `playlists3Plus.sync`. The Plus manifest lists all four
playlists with the same `Dev_ID`s the classic pdb uses:

```
<NODE Id="BDF6834D" ParentId="0"        Attribute="1" Dev_ID="2"  Timestamp="0"/>
<NODE Id="67F149CB" ParentId="BDF6834D" Attribute="0" Dev_ID="25" .../>   Intro
<NODE Id="9431CFB3" ParentId="BDF6834D" Attribute="0" Dev_ID="26" .../>   tension builders
<NODE Id="3C36F82D" ParentId="BDF6834D" Attribute="0" Dev_ID="27" .../>   maintain
<NODE Id="4E1C5E9A" ParentId="BDF6834D" Attribute="0" Dev_ID="28" .../>   peak
```

So the playlists exist in both libraries by name, and their membership was
written only to the Plus one. A player that reads Plus shows them full; anything
reading the classic `export.pdb` — this deck, and ProLink's own database
service, which serves `PIONEER/rekordbox/export.pdb` and nothing else — shows
them empty. `exportExt.pdb` was checked and does not hold them either: one
playlist-tree row and no entries.

Worth noting the folder's own node carries `Timestamp="0"` where the working
folder has a real one. Might mean nothing; might be the tell for "never written
to the classic export".

### What to do about it

Two honest options, and the first is not a workaround:

1. **Re-export the stick from rekordbox** so the classic `export.pdb` carries
   the membership. If rekordbox will not write it, that is Pioneer's choice
   about which library is authoritative and the deck cannot argue with it.
2. **Read Device Library Plus.** `exportLibrary.db` is SQLCipher; the key is
   known in the wild but this is a real piece of work and a licensing question,
   not an afternoon. Do not start it without deciding whether the deck wants to
   depend on it.

Until one of those, the deck is showing exactly what the classic library says,
and the display is not lying — it just cannot see the other library.

### What the hunt left behind

- `MediaRegistry` keeps the bytes it ingested at `~/.mixxx/last-ingest.pdb`. A
  remote medium's pdb never touches the disk, so "why was this playlist empty"
  had no evidence to work from. MIDI note 0x7B re-pulls the database.
- `lib/prolink` gains three examples: `pl_probe`, `entries_pages`, `all_pages`.
  Between them they answer "did we read every row" and "is every row
  attributed", which are the two ways this could have been our fault.
- A trap: `QSqlDatabase::databaseName()` is a URL here (`file:/home/...`), not a
  path, so the first version of the pdb copy wrote to a mangled directory and
  failed silently.
- **A correction worth keeping.** The first pass concluded "not our bug" on the
  strength of the orphan count alone. That was too strong: zero orphans proves
  every row we *read* was attributed, not that we read every row. The chain walk
  is what actually closes it. Same conclusion, arrived at properly the second
  time.

# History

Everything below is settled and is kept for the reasons rather than the results:
each of these cost hours, and the next person to meet one of them — including
me, next month — will meet it in the same disguise.

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

## Resolved: the codec leaked its input to its output — it was the MONITOR switch

**Fixed by setting MONITOR to OFF on the UCA222.** The dry stopped coming back.

**The manual is wrong about this unit**, and it cost an hour, so it is worth
stating plainly. The UCA222 user guide says of the switch:

> With the MONITOR switch OFF, the headphone output receives the signal from the
> computer over the USB port (same as the RCA output jacks). With the MONITOR
> switch ON, the headphones receive the signal connected to the RCA INPUT jacks.

Headphones, in both positions. The RCA `OUTPUT` is documented throughout as
carrying the computer's audio and nothing else. On this unit it does not: with
MONITOR ON, the input reaches the **RCA outputs** as well. Every measurement
below was consistent with a hardware analog path and the reasoning was sound —
the error was trusting the manual to describe where that path went.

**Lesson for the rest of this work:** on this hardware, flip the switch and
listen before reasoning from the documentation.

What had been established, all of it still true:

- With the CDJ's channel fader **fully down** and its AUX 2 send up, the CDJ is
  audible on TriMixxx's mixer channel.
- It survives with **Mixxx stopped**, and appears during boot **before Mixxx
  starts**. Powering the Pi down kills it.
- **Nothing in software is doing it.** The capture stream reads `Status: Stop`,
  so the ADC's data never crosses USB; only `mixxx` holds the device and only
  its *playback* node; no PipeWire, PulseAudio, alsaloop or jackd is running;
  no `.asoundrc` and no `/etc/asound.conf`.
- **Nothing in the codec's declared topology is doing it either.** The USB audio
  descriptors are two disjoint chains — `[1] USB Streaming → [3] Feature Unit →
  [2] Speaker` and `[4] Analog In → [5] USB Streaming` — with no Mixer Unit and
  no Selector Unit anywhere, which are the only descriptors that could route
  capture into playback.
- The UCA222 manual is explicit that the MONITOR switch feeds the **headphones
  only**: OFF gives the phones the computer's signal, ON gives them the RCA
  input. The RCA `OUTPUT` is documented as carrying the computer's audio in both
  positions. Only the RCA jacks are in use here.

So it was analog and undocumented — which is exactly what the MONITOR switch
turned out to be, routed somewhere the manual does not admit to.

**Still unexplained, and probably nothing:** the near-mono capture measured
before the switch was found — L/R correlation 0.993 with side energy pinned at
−24.5 dB across 30 s. The ground-coupling theory would have explained it, since
a common-mode component lands identically on both channels; with that theory
gone, the likeliest remaining explanation is simply a very mono techno loop.
Settle it whenever convenient by unplugging the `AUX 2 OUT` L cable: if L drops
to the noise floor while R keeps signal, the channels are independent and it was
the music.

## Fixed: Mixxx never wrote its settings on exit

Found 2026-08-11 while checking that per-slot wet round-tripped. **It does not,
and neither does anything else**: `~/.mixxx/effects.xml` and `mixxx.cfg` were
last modified by an `scp`, not by Mixxx, across many restarts since.

What is established:

- `soundconfig.xml` **is** written, at 22:39:48, i.e. at *startup*. So Mixxx can
  write to that directory and the permissions are fine. It is specifically the
  **shutdown** save path that never runs — which is where both `mixxx.cfg` and
  `effects.xml` are written.
- The signal handler works. `PosixSignalHandler` logs `caught signal 15 -
  shutting down` on every restart, and the shutdown proceeds far enough to close
  the main window, process `QEvent::Quit` and spend 70 ms deleting the skin.
- **Not a crash.** No segfault in `dmesg` or the journal. The log simply stops,
  which is consistent with buffered Debug output being lost at exit — the deck
  runs with `--log-flush-level info`.
### Root cause: Mixxx crashes on shutdown

Found by logging the exit status from `xinitrc`, which is otherwise unknowable —
Mixxx's own log just stops at whatever was last flushed, so a clean quit and a
crash look identical from outside.

```
debug [Main] 25 ms deleting skin
malloc_consolidate(): unaligned fastbin chunk detected
Aborted
--- mixxx exited 134 at 22:50:43 ---
```

`malloc_consolidate(): unaligned fastbin chunk` is **glibc's heap-corruption
detector**, not a failed assertion. Something has written outside an allocation
or freed twice, and glibc notices during the teardown that follows skin
deletion. The process dies there — before `CoreServices::finalize()` reaches
`CLEAR_AND_CHECK_DELETED(m_pEffectsManager)` (which is what writes
`effects.xml`) and before `~CoreServices` reaches `m_pSettingsManager->save()`
(which writes `mixxx.cfg`). Hence neither file is ever written.

**Three shutdowns, three different faults: 134 (SIGABRT), 135 (SIGBUS), 139
(SIGSEGV).** Varying with allocation layout is the signature of heap corruption
rather than one consistently bad pointer.

**It is not our skin.** Swapping `ResizableSkin` to stock `LateNight` and
repeating gave 135 — still a crash on shutdown. So `WDeckBrowser`,
`WDeckEffects` and the rest of the deck's widgets are not the cause, and this
is not a regression from the effect-pedal work.

### Bisected: `TrackCache` outlives its background copies — **fixed**

`git bisect` over the 85 commits between `6b272c0` (the signal handler, the
first commit where a clean shutdown was even attempted) and `main` landed on
**`e4cbfeb` "deck: the deck plays from a copy, never from the medium"**, which
introduces `src/library/deck/trackcache.cpp`.

`TrackCache::prefetch()` hands a lambda capturing `this` to the **global** thread
pool, copies a track — seconds, for a big file — and then dereferences `this`
again to invoke a continuation that touches `m_entries` and `m_ramBytes`. The
destructor cleared a static pointer and nothing else. A copy still running when
the cache is destroyed therefore writes into freed memory, on every shutdown.

Fixed by giving it its own `QThreadPool`, drained in the destructor with
`clear()` then `waitForDone()`. That also makes it run one copy at a time, which
is what `prefetch`'s own comment already claimed ("the constraint is the USB
bus, not the CPU") but the global pool did not do.

**Verified: exit 0 and `mixxx.cfg` written**, where it was 135 and untouched.
Both settings files now persist, and `effects.xml` round-trips `WET` properly
for the first time.

### Two things learned in the process

**Old commits do not build against today's Debian.** `mixxx-test` links
`GTest::gmock`, which only main's `-DBUILD_TESTING=OFF` avoids, and
`.dockerignore` did not exist before a certain commit — without it the 9.3 GB
Rust `target/` directory ships as build context and fills Docker's VM. Both were
handled by pinning main's `Dockerfile` and `.dockerignore` on every bisect step;
they are build recipe, not product code, and holding them constant is what a
bisect wants anyway. **Keep doing that for any future bisect.**

**Restoring config while Mixxx runs no longer works.** Copying `effects.xml`
into place and then restarting lets the *running* instance write its stale
in-memory state over the restore — the exact hazard `upload.sh`'s comment
describes. It only bites now because the save works. Always stop first.

### The "second crash" does not reproduce — withdrawn

Right after the TrackCache fix, one shutdown with our skin still gave 135 while
stock LateNight gave 0, and that was written up here as a second, skin-specific
bug. It is not.

Three consecutive shutdown tests with the TriMixxx skin now give **exit 0 and
both settings files written**. The single 135 came from the one test where
config had just been restored underneath a *running* Mixxx — an instance holding
stale state that then wrote it back — so the likeliest reading is the same
corruption, or an artefact of that sequence, rather than a separate defect.

Nothing further to hunt. If it reappears the harness is still here: the exit
status logging in `xinitrc`, and `git bisect` with main's `Dockerfile` and
`.dockerignore` pinned on every step.
   damage is limited to state the DJ changes in the UI.

**This invalidates two earlier conclusions in this document.** Both were reached
by reading `effects.xml` after a restart and taking it for Mixxx's output:

1. That the WET mix mode "round-tripped through the control", proving the patch
   was live. It was my own uploaded file. The patch *is* live — the audio proves
   that — but that verification was worthless.
2. That an empty `<Parameters>` list "re-serialises the same broken state on
   exit", making it self-perpetuating. Nothing was serialised at all. The
   parameters really were zero, which the Effects page confirmed independently,
   but the mechanism was wrong.

**Consequence for the rack:** named rack presets are safe —
`EffectChainPresetManager::savePreset()` writes immediately. The *live* rack
would not survive a reboot until this is fixed.

**Unrelated, found alongside:** `~/.mixxx/stderr.log` is **346 MB** and growing.
Nothing rotates it.
