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

## Open: Mixxx never writes its settings on exit

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

### Still open: a second crash, later in shutdown

After both saves complete, shutdown still ends in 135 (SIGBUS) — **with our skin
only**; stock LateNight exits 0. So there is a second memory bug in the deck's
own widgets, and unlike the first it costs nothing functional. The bisect
harness (`worklog/effect-pedal/`, and the exit-status logging now in `xinitrc`)
makes finding it cheap to repeat.

Where to go next on that one, cheapest first:

1. **Reproduce off the deck.** A desktop build of the fork, same shutdown path.
   If it reproduces, everything below is easy; if it does not, it is arm64,
   this Qt, or the GL driver.
2. **ASan.** `-fsanitize=address` names the offending allocation directly and
   is the fastest route to an answer if step 1 reproduces.
3. **Bisect the fork** against upstream 2.5.6, which is the branch point.
4. Until then, config on the deck is `scp`-managed anyway, so the practical
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

## Open questions

- Which mixer channel does TriMixxx return on? A music channel (CH 1-4) gets the
  VCF and crossfader, which the deck wants as a player; `RTN 1/2` does not.
- Does the deck's own track ever want the pedal bus? It can't — that closes the
  feedback loop. Its effects have to be internal (a second unit on
  `[Channel1]`), which means two FX controls unless the tab hides the seam.
- Is a second cheap USB DAC worth it later? It would give the wet return its own
  mixer channel and break the coupling where fading TriMixxx out also pulls the
  effect off every other channel.
