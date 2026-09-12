# Dimensions

This document is the final authority on EVERY dimension used in the CAD of the TriMixxx assembly V2.
It is formatted like this:

- `name_of_the_dimension` Purpose of the dimension `123.45 mm`

Those are then used in Fusion360. Any revision to those dimensions is also noted here, with notes as to why revision was needed.

## Axis
From front to back (z axis) is called `depth`
From left to right (x axis) is `width`
From bottom to top (y axis) is called `height`

Some components may have their own frame of reference, e.g. the screen is tilted to it may have a need for that.

## Trimixxx V2 Dimensions

### Case main dimensions
All the dims of the case itself, the bedrock of the model.

- `body_depth` if you imagine the unit as a rectangle, the depth of that, `423 mm`
- `body_width` - `329mm`
- `body_height` this is excluding the protrusion from the angled up screen, or any button. It's just the main case body, as a rectangle, from floor to top surface. `85 mm`

- `body_ledge_height` from the top surface, towards the floor, there is a "band" of fixed width, which we keep for clearance for internals. It is strictly less than `body_height`. `25 mm`
- `body_foot_section` the width of the square feet, `40 mm`

### Screen

The manufacturer's measurements are not correct. These are measured from the received article.
Good fucking thing I didn't design the mounting for it before it arrived fml smh fr fr skibbidi.

`screen_inside_*` is the area elevated from the glass, on the backside of the screen.
`screen_total` is the entire footprint of the screen (with the glass).

`screen_inside_depth` 143.11mm
`screen_inside_width` 228mm
`screen_total_depth` 147.1mm
`screen_total_width` 239mm


`screen_inside_left_margin` 3mm
`screen_inside_right_margin` 7.65mm
`screen_inside_top_margin` 1.9mm
`screen_inside_bottom_margin` 1.9mm

`screen_inside_thickness` 4.0mm
`screen_glass_thickness` 1.25mm
`screen_total_thickness` 5.3mm
`screen_total_thickness` = `screen_glass_thickness` + `screen_inside_thickness`
 ~= 4.0+1.25 = 5.25 ~= 5.3
The PCB thickness is excluded because it's a small rectangle in the centre.

`screen_pcb_width` 110mm
`screen_pcb_depth` 76mm
`screen_pcb_thickness` 12.3mm

`screen_round_dia` the glass is rounded like this 3.6mm

On the left side of the display (when looking at it from the backside, with PCB text correctly oriented), there are some ribbon cables routing on the left edge. This area should be avoided.
It starts at `screen_inside_dangerzone_topleft` from the top left of the screen_inside rectangle and ends at `screen_inside_dangerzone_bottomleft` from the bottom left of the screen_inside rectangle.
`screen_inside_dangerzone_topleft` 32mm
`screen_inside_dangerzone_bottomleft` 34mm

`screen_view_bezel_*` is the bezel on the glass side around the viewing display area (the black gaps on the side)
`screen_view_bezel_right` 11mm
`screen_view_bezel_bottom` 5.5mm
`screen_view_bezel_left`  12mm
`screen_view_bezel_top` 6mm

### Connectors

IEC power connector:

- `iec_width` 30.6mm
- `iec_height` 22.5mm
- `iec_holes_dist` 40mm
- `iec_screw_dia` 1.55mm

LAN:

- `lan_height` 19.45mm
- `lan_width` 14.79mm
- `lan_wall_thickness` the wall around needs to be this thickness for the clip to engage 1.55mm
Look up the datasheet for https://www.kycon.com/Pub_Eng_Draw/KLAPX-CPLR-N-88.pdf

RCA:

- `rca_hole_dia` 14.2mm

### Standoff boards

Pi (m2.5):
- `pi_standoff_width` 49mm
- `pi_standoff_depth` 58mm

S3 Midi board (m3)
- `s3_pcb_width` 44mm
- `s3_pcb_depth` 65mm

IRM-30-5ST PSU (m3)
- `irm_width` 22.7mm
- `irm_depth` 80.6mm

UCA222 DAC (m3)
- `dac_width` 67.1mm
- `dac_height` 18mm

### Heatset inserts

- `m3_heatset_dia` 4.0mm
- `m3_heatset_wall` ~~1.6mm~~ 1.8mm for robustness
- `m3_heatset_depth` 6.7mm

- `m25_heatset_dia` 3.9mm
- `m25_heatset_wall` ~~1.6mm~~ 1.8mm for robustness
- `m25_heatset_depth` 6mm

### Top half screen controls

- `jog_dia` 210mm
- `playcue_dist` distance between the button's centres 47mm
- `loop_start_end_dist` 31.9mm
- `loop_end_reloop_dist` 35mm
- `tempo_knob_width` 16.5mm
- `tempo_length` 130mm
- `onebtn_dia` 15mm