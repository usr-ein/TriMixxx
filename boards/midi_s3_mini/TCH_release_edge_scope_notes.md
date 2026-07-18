# TriMixxx — JOG_TCH release-edge scope test

**Field lab notes.** Goal: find out why IO14 keeps dying. Hypothesis: the 20-year-old membrane touch switch rings **below GND** on the *release* (opening) edge, and that negative excursion punches the S3's lower clamp diode.

**No S3 in this test.** You are recreating the pin's environment with a bench PSU + scope so nothing dies. Bring the jog assembly, the proofboard, and the salvaged ribbon connector — the *actual* suspect parts.

---

## What you're testing (read first)

- The fault is on **release**, not press. Contact opening on a worn membrane = micro-arcs + L·di/dt kick into the C8/pull-up loop → possible ring below 0 V.
- It's **intermittent**. You must actuate **many times** (50–100+) and also spin/flex to catch it. One clean press proves nothing.
- A clean switch can't do this. A decrepit one can. You're checking whether *this specific sensor* misbehaves vs a known-good switch.

**Success = seeing the TCH node go below about −0.3 V on any release.** That's the kill signature.

---

## Bring

- Jog assembly (optical encoder + membrane sensor) + its ribbon + the salvaged connector + your proofboard
- Bench PSU (set **3.3 V**, current limit **~100 mA**)
- Scope + one 10× probe (RIGOL DS1000Z likely — settings below map to any DSO)
- DMM
- 1× **10 kΩ** resistor (the pull-up, = R5)
- 1× **100 nF** cap (= C8)
- 1× **known-good tactile switch** (for the A/B comparison)
- Breadboard + jumpers, short probe ground lead / spring tip if you have one

---

## STEP 0 — DMM checks BEFORE powering (may close it in the parking lot)

1. **Sensor resistance, while abusing it.** Meter across the membrane switch. Press = should read a clean low (tens of Ω or less); release = clean open (OL). Now **flex the ribbon and wiggle the platter** while watching:
   - Flickering resistance, intermittent low-when-released, or noisy readings → **membrane is degraded. Suspect confirmed, you can stop here.**
2. **Isolation.** Meter from **TCH (J11.1)** to *every* other conductor on the proofboard: GND, 3V3, the two encoder lines, and 5 V if present anywhere. Wiggle the stretched-pitch JSTs while measuring.
   - Anything other than open (except the switch path to GND) = a short/leak in the harness. Found it, no scope needed.

If both are clean, power up and scope.

---

## STEP 1 — Wire this

```
        +3.3 V  ← bench PSU (+), current limit ~100 mA
          |
        [ 10k ]        pull-up (R5)
          |
   TCH  --+-------------------- SCOPE CH1  (probe TIP)
          |         |
        [ SW ]    [ 100n ]      SW = membrane switch,  C8 (put it IN)
       membrane     |
          |         |
         GND -------+----------- SCOPE CH1  (GROUND clip — keep SHORT)
          |
        bench PSU (-)
```

- 10 kΩ: TCH → +3.3 V
- Membrane switch: TCH → GND (this is the sensor under test)
- 100 nF: TCH → GND (in for the first pass — it's half the LC that would ring)
- Probe tip on **TCH**, ground clip on **GND**, and keep that ground lead **as short as possible** — a long clip adds its own ring and you'll blame the switch for a probe artifact.
- PSU ground = scope ground = board GND (common).

---

## STEP 2 — Scope setup

**Settings at a glance**

| Setting | Value | Why |
|---|---|---|
| Probe | 10× (compensate first) | Standard; less loading |
| Coupling | **DC** | You must see the absolute level incl. below 0 V |
| Vertical | ~500 mV/div | Resolve a few-hundred-mV undershoot |
| Vertical position | Put **0 V about 2/3 up** the screen | Leaves room to see (and trigger on) negative excursions |
| Timebase (survey) | ~1 ms/div | See the whole press→release shape |
| Timebase (edge hunt) | ~1–10 µs/div | The ring lives in the first µs of release |
| Trigger type | Edge | — |
| Trigger source | CH1 | — |
| Trigger slope | **Falling** | Release edge (voltage going down toward/through GND) |
| Trigger level (survey) | ~1.5 V | Catch the general release edge |
| Trigger level (hunt) | **−0.2 to −0.3 V** | Fires ONLY on a real below-GND event |
| Sweep/mode | **Normal** to watch live, **Single** to freeze a glitch | Normal keeps updating; Single arms + captures the first undershoot |
| Bandwidth | Full (20 MHz limit OFF) | The killing edge is fast; don't filter it out |

Note: the trigger level must be **on-screen** — that's why 0 V is positioned high, so −0.3 V is still visible below it.

**RIGOL DS1000Z button path**
- Probe: press **CH1** → *Probe* → **10X**
- Coupling: **CH1** → *Coupling* → **DC**
- Vertical scale/position: the two **CH1** rotaries (big = V/div, small = position)
- Timebase: **horizontal** rotary
- Trigger: **Menu** in the Trigger block → *Type* = **Edge**, *Source* = **CH1**, *Slope* = **Falling**; set level with the **Trigger Level** knob
- Sweep mode: Trigger menu → *Sweep* = **Normal**; to freeze one event press the **Single** button
- Zoom into an edge after capture with the horizontal knob (or Zoom/Delayed sweep)

---

## STEP 3 — Run it

1. **Survey pass** (1 ms/div, falling trigger ~1.5 V, Normal sweep): press and release slowly a few times. Learn the normal shape. Note if release looks ragged/multi-edge (bounce) vs a clean rise.
2. **Undershoot hunt** (zoom to ~1–10 µs/div on the release edge, trigger level **−0.25 V**, **Single**): arm it, then **press/release 50–100 times**, and also **spin the platter** and **flex the ribbon** between presses. Re-arm Single after each catch.
   - You're waiting for the scope to trigger at all. If it triggers on the negative level, capture it — that's the glitch. Read how far below 0 V and how it rings.
3. **A/B control:** swap in the **known-good tactile switch**, same rig, repeat the hunt. If the tactile stays clean and the old membrane goes negative → **case closed, it's the sensor.**
4. **C8 in vs out:** repeat once with the 100 nF removed. If the undershoot changes size/shape, C8 is part of the LC ring (useful for the fix). If it dies exactly the same, the sensor's own dynamics dominate.

Optional: put CH2 on the +3.3 V rail, trigger on CH1 — a rail dip coincident with a TCH event would point at a supply/coupling problem instead. (Lower priority; encoder is 3V3 and on the far side of the board.)

---

## What the result means

- **TCH goes below ~−0.3 V on release (membrane) but tactile is clean** → confirmed: aged switch injecting below-GND into the lower clamp. Fix: retire the 2005 membrane; add **Schottky pin→GND** (cathode to pin) to divert the undershoot; add **1 kΩ series** to bound it. Both are inert in normal 0–3.3 V operation.
- **Everything clean, even after heavy actuation + flex** → harness and sensor exonerated. Back to the power-rail class (dual-supply / USB back-feed) as the remaining cause; fix is grounding/OR-ing discipline + Rs+TVS on the respin.

---

## Safety / gotchas

- PSU current limit **~100 mA** — a real short trips it instead of cooking parts.
- **DC coupling** is non-negotiable; AC coupling hides the exact thing you're hunting.
- **Short probe ground** on the edge hunt, or you'll capture probe ring and misdiagnose.
- Don't trust the membrane after this regardless — even if today's pass looks clean, it's 20 years old and abused.
