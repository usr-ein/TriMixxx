# Dimensions

## CDJ dimensions
### CDJ 2000 nexus 2
414 x 320 x 113 mm  cdj nexus 2 (jog 206mm)

### CDJ 3000 (we model this)

- 453 x 329 x 118.1 mm cdj 3000 (jog 206mm == 8.11 in)

- cdj3000 has a "main body" height of 88mm.

- The height of 118.1mm is including the screen protrusion up top
- The "main body" depth is not 453mm, but 423mm. 453mm is including the screen protrusion
- 329mm is the main body width, that's all good.

- Up to screen join's depth (jogwheel containing panel): 300mm

## CDJ Trimixxx Dimensions

- Main body Depth: 423mm
- Full depth with screen: 453mm
- Width: 329mm
- Full Height: 118mm

### Main PCB

pcb_width 250
pcb_depth 104

### Bottom tray

In height, we leave 5mm for the top tray to "sit on top" and combine to 88mm, which is the main body height of the cdj 3000

#### Jog half (JH)

- Height: 83mm
- Width: 329mm
- Depth: 250mm 

Depth cannot be longer than 270mm to allow the PCB to side in the screen half.

#### Screen half (SH)

- Height: 83mm
- Width: 329mm
- Depth: 182mm  (432-250=182mm)

### Top plate

### Jog half (JH)

- Height 6mm (88-83=6mm)
- Width: 329mm
- Depth: 340mm

For max stability, the depth of the jog half is as long as can be, so we can rigidify the structure of the bottom half with it.
340mm is the max (print bed 350mm)

#### Jogwheel

- `jw_over_od` outer dia topside (incl ring) 210mm
- `jw_over_id` inner dia topsidr (excl ring) 180mm
- `jw_under_screwplace_dia` diameter of the circle along which screw holes are present on the underside 226.3mm
  - 2026-05-17: finally got printer. Doing test fits revealed that diameter is 4.1mm oversized. For keeping some clearances, let's reduce by 3.9mm: 226.3 - 3.9 = 222.4mm
  - V2/V3: **`222.4mm` is correct dia** after test fitting.
- `jw_under_dia` dia of the mountplate 230mm

All angles are measured from the triangular protruding piece, which we consider 0deg. Counterclockwise is positive, clockwise is negative.
Left of the 0 is +1, right of the 0 is -1.
Angles are when looking downward, towards the top side of the jogwheel.

- `jw_tighten_angle` angle of the dial to tighten the jog: -37.7deg
	- V4: Jog test fit revealed we were **too low by 1.7mm**. So I did some maths where I turned `jw_tighten_dist` and `jw_tighten_angle` into polar coordinates for the position, then turned those to cartesian, then added 1.7mm to the y component, then turned that back to polar to get the new angle and distance. The new angle is `-36.94deg`
	- V5: I adjusted 1.7mm the wrong fucking way (classic me), the new angle is `-38.456deg`
- `jw_tighten_dist` dist from center to tighten dial 128mm
	- V4: See above, the new distance is now `128.227 mm` 
	- V5: Fucked up the adjustement direction, new distance is `127.795mm`
- `jw_tighten_btn_dia` the dial's knob diameter 13mm
- `jw_tighten_btn_h` the dial's knob height 8mm
- `jw_tighten_btn_stem_dia` the dial's knob's stem's diameter 9mm
- `jw_tighten_btn_stem_h` the dial's knob's stem's height 8mm

Angle of screw holes (s1,s2,s3,s4), numbered clockwise around.

- `jw_s1_angle` -58.7deg
  - V1: This angle was off. should be D=4.7mm to the right.
  - V1: `2*arcsin(4.7/226.3)*180/pi = 2.38011 deg` 
  - V1: `-58.7 - 2.38011 = -61.08 deg`
  - V2: needs 4mm to the left `2*arcsin(4/222.4)*180/pi=2.06 deg` so angle is now `-61.08+2.06 = -59.02 deg`
  - V3: **`-59.02 deg` is correct**
- `jw_s2_angle` -149.2deg
  - V1: This angle was off. should be D=1.6mm to the left.
  - V1: This means the angle should be adjusted to be `delta = 2*arcsin(D/dia) = 2*arcsin(1.6/226.3)=0.01414 rad = 0.01414 rad*180/pi = 0.810199 deg`
  - V1: So after V1, angle is now `-149.2 - 0.810199=-150.1`
  - V2: needs 2.4mm to the left `2*arcsin(2.4/222.4)*180/pi=1.24 deg` so angle is now `-150.1+1.24 = -148.86 deg`
  - V3: **`-148.86 deg` is correct**
- `jw_s3_angle` 151.1deg
	- V1: OK, **`151.1 deg` is correct**
- `jw_s4_angle` 60.5deg
  - V1: This angle was off. should be D=2.1mm to the left.
  - V1: `2*arcsin(2.1/226.3)*180/pi = 1.06339 deg`
  - V1: New angle is `60.5+1.06339=61.5634 deg`
  - V2: OK, **`61.5634 deg is correct`**
- `jw_screw_dia` 4.5mm

- `jw_over_h` from top face to just below the ring 17mm
- `jw_interspace_h` height between below ring end of 45deg slope, and end of skirt
- `jw_skirt_w` the jogwheel ring profile goes flat top -> 45 deg -> vertical (aka interspace) -> skirt. This is the width of the skirt, flaring out of the bottom of the interspace: 2.5mm
- `jw_skirt_h` the height of said skirt (thickness) 1.5mm
- `jw_mountplate_h` height of the plate with the holes in it 7mm

Derived from modelling:

`jw_tp_gap_h` the gap between the jw mountplate, and the top plate's underside, measured after placing the jogwheel assembly in position: 2.0mm

#### Tempo fader

- `tp_tempo_board_h` The pcb board sits at 9.5mm away from the top plate.
- `tp_tempo_cut_w` the cut groove where the fader can move up and down in, estimated at 4mm for margins
- `tp_tempo_rest_w` the width of the resting groove where the fader handle sliders in, 20mm
- `tp_tempo_rest_h` the height of the resting groove where the fader handle sliders in, measured from top plate surface, 3.5mm

#### Buttons

- `btn_play_dia` like original 38mm
- `btn_play_x` like original 28mm
- `btn_play_y` more than original, to avoid mounting hole 39mm
- `btn_play_cue_dist` between centres of the buttons 47mm
- `btn_play_height_over` the extra height of the button over the top plate's surface. 1.5mm

- `btn_loop_dia` like original 15mm
- `btn_reloop_dia` like original 9mm
- `btn_loop_start_x_jog` measure of x from jog center to loop start btn center 125mm (orig 103mm)
- `btn_loop_start_y_jog` measure of y from jog center to loop start btn center 119mm (orig 126mm)
- `btn_loop_start_end_dist` between centres of the buttons 32mm (orig 36mm)
- `btn_loop_end_reloop_dist` between centres of the buttons 35mm (orig 51mm)

- `btn_pcb_standoff_h` the height between the pcb surface and the top plate's bottom face. These PCB carry buttons and need to be away from the bottom face to make space for the jogwheel assembly: 12mm
- The height of a tacticle THT button switch. I have many, but let's pick the smallest button that way I can shorten the plastic button I print later if need be.
  - `btn_switch_free_h` h=4.8mm soldered to the board but NOT DEPRESSED
  - `btn_switch_pressed_h` h=4.5mm soldered to the board when PRESSED
- `btn_switch_width` 6mm, it's a square. The black press portion inside is slighlty smaller.
- `btn_led_dia` 4.8mm
- `btn_led_light_guide_wall` 2.5mm
- `btn_led_h` 8.3mm (+3mm for solder) (flat soldered, but I can probably not reach to solder the top, and my bust ass custom PCBs don't have rivetted vias yet, so let's leave a bit extra room to fit the iron
- `btn_led_dist` imagine a square around the central button, the square's centre is the button. The LEDs are in the corners. This is the square's side. 7.5mm

### Screen half (SH)

This dimension is a bit more complex (angle for the screen etc).
But we'll have a rect encompassing it like this:

- Height: variable
  - min 6mm
  - max 35.1mm (118.1-83=35.1)
- Width 329mm
- Depth: 153mm (453-300=153mm)
- Overhang with Top plate jog half `tp_sh_overhang_jh`: 50mm
	- The top plate sh overhangso onto the jog half a bit to provide stability and have the screen angled.
	- NOTE 2026-05-19: So I fucked up and my H2D doesn't print that size. SO I need to reduce this overhang, but I can't be arsed to redesign. I also already printed. So I will just add a second variable to counteract my bad past design choices... Gods of CAD please be merciful on this wretched soul.
	-  Overhang shameful correction factor: `tp_sh_overhang_jh_new`: 10mm
- Overhang height (thickness of it) `tp_sh_overhang_h`: 3mm

#### LCD Screen

- `scr_width` screen width: 210mm
- `scr_height` screen height: 126mm
- `scr_thickness` **ASSUMED, MEASURE LATER** 5mm (3.5mm to 5mm)
- `scr_inside_w` inside the view area width: 198mm
- `scr_inside_h` inside the view area height: 113mm
- `scr_bezel_left`: 5mm 
- `scr_bezel_right`: 7mm
- `scr_bezel_up`: 4.2mm
- `scr_bezel_down`: 9.2mm
- `scr_connector_x`: 87mm
- `scr_connector_depth`: 47mm
- `scr_connector_width`: 42mm

#### Display plane

The rect inside the screen half that contains the display

- `display_depth` Depth 145mm
	- Larger than scr_height = 126

- `display_width` Width 230mm
 	- Larger than scr_width = 210
- `display_inner_wall` inside the hollowed out screen display: 3mm

- `display_back_height` thickness of the display plane at the back of the unit 30mm
- `display_scr_from_bottom` how far from bttom of the display plane does the screen starts (0: starts at the jog, max: reaches the top of the display plane): 10mm
- `display_angle` angle away from the plane 5deg

#### Display retainer clips

These are screwed in 4 corners of the display assembly to the TP_SH's display_top_face. They keep the screen flush against the panel.

- `display_clips_screw_height` the thickness of it where it engages with the screw/screwpost `3mm`
- `display_clips_depth` how large (y axis) is it `10mm`
- `display_clips_push` how big is the lever push thingy that push against the screen. Too big and the piece will crack. `1.3mm`

#### Hardware on the screen half

- `tp_sh_encoder_y` how far is the encoder in y from the top edge of the TP_SH `25mm`
- `tp_sh_usb_y` how far is the usb port center in y from the top edge of the TP_SH `20mm`

# Screws

## Screws between Top Plate and Bottom Tub

We use M3x80. This means:

- M (metric thread)
- 3 (in mm thread major diameter)
- x80 (length of 80mm from underside of head to tip)

They're pitched at 0.5 by standard.

### Lengths

- `s_len` Screw thread length (tip to underside of head): 80 mm
- Screw total length (tip to top of head): 83 mm
- `s_head_len` Screw head height: 3.0 mm nominal
- `s_insert_h` Insert length: 5.7mm

### Diameters

- Thread major diameter: 3.0 mm nominal (min 2.98mm)
- `s_head_dia` Head diameter: 5.5 mm nominal (5.68 mm max)
- `s_insert_od` screw insert outer diameter 4.7mm
- `s_bore_dia` Bore hole (the hole where the screwdriver/allen key fits) diameter: 7mm
