# OneButton PCB — KiCad 10 Manufacturing Constraint Presets

Board: OneButton, 20 × 20 mm. Three rule-set presets below.
All field names are the **KiCad 10.0** labels (Board Setup → Design Rules → *Constraints* / *Net Classes*, and Copper Zone Properties). JLCPCB numbers are taken from the official capability page you saved (`jlcpcb_specs_2026-06-29.html`), FR-4 **2-layer, 1 oz outer copper**.

Convention used here: the **Constraints** tab values are the *absolute floor* for that preset (DRC hard minimum, cannot be overridden looser). **Net Classes** carry the *working* values (≥ the floor). **Zones** are set per preset.

---

## 0. JLCPCB capability floor (reference — 2-layer, 1 oz)

What the fab can actually do. Presets sit above these with margin.

| Capability | JLCPCB floor (2-layer, 1 oz) | Notes |
|---|---|---|
| Min track width / spacing | **0.10 / 0.10 mm** (4/4 mil) | 3.5 mil is *multilayer only* |
| Track width tolerance | ±20% | 0.10 mm finishes 0.08–0.12 mm |
| Min via: hole / diameter | **0.15 / 0.25 mm** | diameter ≥ hole + 0.1 mm (0.15 preferred) |
| Via surcharge trigger | hole 0.15 (any dia); **or** hole 0.20/0.25 with dia < 0.45 | no-cost via = hole ≥ 0.20 **and** dia ≥ 0.45 |
| Preferred min via hole | 0.20 mm | |
| Min drill (PTH) | 0.15 mm (more costly) | recommend PTH ≥ 0.50 mm to avoid mask/tin in hole |
| Min NPTH | 0.50 mm | |
| PTH annular ring | abs. min 0.18 mm, **rec ≥ 0.25 mm** | NPTH pad ring ≥ 0.45 mm |
| Via hole-to-hole spacing | 0.20 mm | |
| Pad hole-to-hole spacing | 0.45 mm | |
| Pad-to-track clearance | 0.10 mm | |
| Via-hole-to-track / NPTH-to-track | 0.20 mm | |
| PTH-to-track | 0.28 mm (0.35 rec) | |
| SMD pad-to-pad (diff net) | 0.15 mm | min SMD pad 0.25 × 0.25 mm |
| Copper-to-edge (routed) | 0.20 mm | V-cut edge: 0.40 mm |
| Solder-mask expansion | **1:1** (since Jun 2025 LDI) | set KiCad expansion = 0; keep ≥ 0.09 mm mask-to-trace |
| Solder-mask bridge (1 oz) | 0.10 mm (green/red/yellow/blue/purple); 0.13 mm (black/white) | |
| Silk min line width / text height | 0.15 mm / 1.0 mm | pad-to-silk 0.15 mm |
| Min board size | 3 × 3 mm | 20 × 20 is fine; panelize tiny boards |

**Blind/buried vias and microvias: NOT supported.** → uVia constraints below are inert for JLC presets; leave at KiCad defaults and never place a uVia.

These live outside the *Constraints* tab and are global per board — set once:

| Board Setup page | Field | JLC value |
|---|---|---|
| Solder Mask/Paste | Solder mask expansion | **0 mm** (1:1) |
| Solder Mask/Paste | Solder mask min web (bridge) | 0.10 mm (green); 0.13 mm (black/white) |
| Constraints (top) | Arc/circle max deviation | 0.005 mm (geometry only, not a fab limit) |

---

## Use Case 1 — Thin, low-power board (tight, small margin)

JLC 2-layer 1 oz. Thin traces, small vias, *no surcharge* (vias kept at hole ≥ 0.20 / dia ≥ 0.45). Floor is 0.10/0.10; this sits ~1.3× above to survive the ±20% etch tolerance.

### Constraints (Design Rules → Constraints)

| Group | Field | Value | Rationale |
|---|---|---|---|
| Copper | Minimum clearance | 0.13 mm | ~1.3× over 0.10 floor |
| Copper | Minimum track width | 0.13 mm | finishes 0.10–0.16 mm at ±20% |
| Copper | Minimum connection width | 0.13 mm | catches neck-downs |
| Copper | Minimum annular width | 0.13 mm | passes the 0.45/0.20 via (0.125 ring); PTH pads use ≥ 0.25 ring in footprint |
| Copper | Minimum via diameter | 0.45 mm | no-surcharge floor |
| Copper | Copper to hole clearance | 0.25 mm | > via-to-track 0.20; PTH-to-track wants 0.28 (see custom rule) |
| Copper | Copper to edge clearance | 0.30 mm | > routed-edge 0.20 |
| Holes | Minimum through hole | 0.20 mm | matches via hole |
| Holes | Hole to hole clearance | 0.20 mm | via floor; pads need 0.45 (custom rule) |
| uVias | Minimum uVia diameter | 0.20 mm | inert (no uVias at JLC) |
| uVias | Minimum uVia hole | 0.10 mm | inert |
| Silkscreen | Minimum item clearance | 0.15 mm | pad-to-silk |
| Silkscreen | Minimum text height | 1.0 mm | |
| Silkscreen | Minimum text thickness | 0.15 mm | |

### Net Classes (Design Rules → Net Classes)

| Class | Clearance | Track Width | Via Size | Via Hole | uVia Size | uVia Hole | DP Width | DP Gap |
|---|---|---|---|---|---|---|---|---|
| Default | 0.15 mm | 0.15 mm | 0.45 mm | 0.20 mm | 0.20 (unused) | 0.10 (unused) | — | — |
| Power | 0.15 mm | 0.30 mm | 0.60 mm | 0.30 mm | — | — | — | — |

### Filled Zone (Copper Zone Properties)

| Field | Value |
|---|---|
| Clearance | 0.15 mm |
| Minimum width | 0.13 mm |
| Pad connections | Thermal reliefs |
| Thermal relief gap | 0.20 mm |
| Thermal spoke width | 0.20 mm |
| Remove islands | Below area limit |
| Minimum island size | 0.20 mm² |
| Corner smoothing | Fillet |
| Fill type | Solid fill |

### Optional custom rule (splits the two hole-to-hole values KiCad can't express globally)

```
(rule "PTH pad hole-to-hole"
  (constraint hole_to_hole (min 0.45mm))
  (condition "A.Type == 'Pad' && B.Type == 'Pad'"))

(rule "PTH to track"
  (constraint hole_clearance (min 0.28mm))
  (condition "A.isPlated() && A.Type == 'Pad'"))

(rule "PTH pad annular ring"
  (constraint annular_width (min 0.25mm))
  (condition "A.Pad_Type == 'Through-hole'"))
```

The global `Minimum annular width 0.13` lets the small 0.45/0.20 vias pass; this rule independently holds THT **pads** to JLC's recommended 0.25 mm ring. THT holes themselves should be ≥ 0.50 mm (mask/tin) — that's a footprint choice, not a DRC floor.

---

## Use Case 2 — Plenty of space (comfortable JLC margin)

Same fab (2-layer 1 oz), but geometry is generous so every value sits well clear of the floor for high yield and easy assembly.

### Constraints (Design Rules → Constraints)

| Group | Field | Value | Rationale |
|---|---|---|---|
| Copper | Minimum clearance | 0.25 mm | 2.5× floor |
| Copper | Minimum track width | 0.25 mm | robust |
| Copper | Minimum connection width | 0.25 mm | |
| Copper | Minimum annular width | 0.20 mm | comfortably above PTH 0.18 min |
| Copper | Minimum via diameter | 0.60 mm | robust, no-surcharge |
| Copper | Copper to hole clearance | 0.30 mm | > PTH-to-track 0.28 |
| Copper | Copper to edge clearance | 0.40 mm | V-cut-safe |
| Holes | Minimum through hole | 0.30 mm | |
| Holes | Hole to hole clearance | 0.45 mm | satisfies pad spacing globally |
| uVias | Minimum uVia diameter | 0.20 mm | inert |
| uVias | Minimum uVia hole | 0.10 mm | inert |
| Silkscreen | Minimum item clearance | 0.20 mm | |
| Silkscreen | Minimum text height | 1.0 mm | |
| Silkscreen | Minimum text thickness | 0.20 mm | |

### Net Classes (Design Rules → Net Classes)

| Class | Clearance | Track Width | Via Size | Via Hole | uVia Size | uVia Hole | DP Width | DP Gap |
|---|---|---|---|---|---|---|---|---|
| Default | 0.25 mm | 0.30 mm | 0.60 mm | 0.30 mm | 0.20 (unused) | 0.10 (unused) | — | — |
| Power | 0.30 mm | 0.50 mm | 0.80 mm | 0.40 mm | — | — | — | — |

### Filled Zone (Copper Zone Properties)

| Field | Value |
|---|---|
| Clearance | 0.30 mm |
| Minimum width | 0.25 mm |
| Pad connections | Thermal reliefs |
| Thermal relief gap | 0.30 mm |
| Thermal spoke width | 0.30 mm |
| Remove islands | Below area limit |
| Minimum island size | 1.0 mm² |
| Corner smoothing | Fillet |
| Fill type | Solid fill |

### Custom rule (THT pads)

```
(rule "PTH pad annular ring"
  (constraint annular_width (min 0.25mm))
  (condition "A.Pad_Type == 'Through-hole'"))
```

Global annular floor is 0.20 mm (covers vias); this holds THT pads to JLC's recommended 0.25 mm ring. Hole-to-hole (0.45) and PTH-to-track (covered by copper-to-hole 0.30 ≥ 0.28) are already satisfied globally in this preset.

---

## Use Case 3 — Home-made laser board (laser ablation + FeCl₃)

Single- or double-sided, THT, generous geometry. Wide margins for ablation edge softness, FeCl₃ undercut (~0.05 mm/side), spray-thickness variation, and hand-drill drift (±0.2 mm). Values carried from the validated laser pipeline; the "latest used were good," so this stays conservative. Microvias and plated holes do not exist in this process — vias are wire/rivet through-holes soldered both sides.

### Constraints (Design Rules → Constraints)

| Group | Field | Value | Rationale |
|---|---|---|---|
| Copper | Minimum clearance | 0.40 mm | absorbs undercut + paint pinholes |
| Copper | Minimum track width | 0.40 mm | floor; default track is 0.50 |
| Copper | Minimum connection width | 0.40 mm | |
| Copper | Minimum annular width | 0.35 mm | process-realistic (nominal ring 0.4–0.5) |
| Copper | Minimum via diameter | 1.80 mm | wire/rivet pad (0.80 hole + 0.50/side) |
| Copper | Copper to hole clearance | 0.40 mm | hand-drill drift |
| Copper | Copper to edge clearance | 1.00 mm | jigsaw / snap cut |
| Holes | Minimum through hole | 0.80 mm | smallest = via/rivet drill |
| Holes | Hole to hole clearance | 0.80 mm | drill registration |
| uVias | Minimum uVia diameter | 0.20 mm | inert (cannot fabricate) |
| uVias | Minimum uVia hole | 0.10 mm | inert |
| Silkscreen | Minimum item clearance | 0.30 mm | ablation-art, coarse |
| Silkscreen | Minimum text height | 1.0 mm | (legend via ablation; usually none) |
| Silkscreen | Minimum text thickness | 0.20 mm | dot-grid resolution |

### Net Classes (Design Rules → Net Classes)

| Class | Clearance | Track Width | Via Size | Via Hole | uVia Size | uVia Hole | DP Width | DP Gap |
|---|---|---|---|---|---|---|---|---|
| Default | 0.40 mm | 0.50 mm | 1.80 mm | 0.80 mm | 0.20 (unused) | 0.10 (unused) | — | — |
| Power | 0.40 mm | 1.00 mm | 1.80 mm | 0.80 mm | — | — | — | — |
| HighCurrent | 0.50 mm | 2.50 mm | 1.80 mm | 0.80 mm | — | — | — | — |

### Filled Zone (Copper Zone Properties)

| Field | Value |
|---|---|
| Clearance | 0.50 mm |
| Minimum width | 0.50 mm |
| Pad connections | Thermal reliefs |
| Thermal relief gap | 0.50 mm |
| Thermal spoke width | 0.50 mm |
| Remove islands | Below area limit |
| Minimum island size | 1.0 mm² |
| Corner smoothing | Fillet (radius 0.50 mm) |
| Fill type | Solid fill |

---

## Notes / open points to confirm before I save this

1. **THT connectors present (confirmed).** For UC1/UC2 this means: connector holes ≥ 0.50 mm (avoids mask/tin in the barrel), pad ring ≥ 0.25 mm (enforced by the *PTH pad annular ring* custom rule), PTH-to-track ≥ 0.28 mm, and pad hole-to-hole ≥ 0.45 mm. The `Minimum through hole 0.20` floor in UC1 still only binds the vias — your connector holes sit well above it. UC3 (laser) is already THT-native, no change.
2. **Use Case 1 tightness** — I set track/clear at 0.13 mm (above the 0.10 floor for ±20% safety). If you want truly tightest, 0.10/0.10 is allowed but leaves no margin. Say the word and I'll drop it.
3. **Use Case 1 vias** — kept at 0.45/0.20 to stay *no-surcharge*. If you want genuinely tiny vias, the floor is 0.25/0.15 but it adds cost — separate preset if so.
4. **Diff Pair Width/Gap** left blank everywhere — OneButton has no differential pairs. Time-domain Tuning Profiles (new in KiCad 10) are also N/A here.
