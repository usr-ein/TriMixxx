# TriMixxx

A custom CDJ (Compact Disc Jockey) unit built from scratch around a Raspberry Pi CM5, designed to run [Mixxx](https://mixxx.org/) DJ software. Features a custom PCB, a custom 3D-printed chassis, and reuses original CDJ buttons and jog wheel for an authentic DJ experience. Reads Rekordbox-formatted USB sticks — no laptop required.

![PCB 3D render](screenshots/pcb-3d-render.png)

## What is this?

TriMixxx replaces the internals of a CDJ with modern, open-source-friendly hardware while keeping the physical controls that DJs know and love. Plug in a Rekordbox-formatted USB stick, and you're ready to mix.

## Hardware Architecture

### Compute

- **Raspberry Pi CM5** — runs Mixxx and handles audio playback, connected via dual DF40 100-pin high-density connectors

### Audio

- **TI PCM5242** — high-quality stereo DAC connected over I2S for low-latency audio output
- **RCA stereo pair** — Left (white) and Right (red) main outputs
- **6.35mm (1/4") headphone jack**
- **3.5mm headphone jack**

### MIDI Controller

- **ATmega32U4** — acts as a USB MIDI class-compliant device, reading the original CDJ's buttons, encoders, fader, and jog wheel and translating them into MIDI messages for Mixxx
- **16 MHz crystal** oscillator for reliable USB timing

All controls connect via JST PH connectors to the main PCB:

| Connector | Controls |
|---|---|
| **J_PLAY** (6-pin) | Play button + LED, Cue button + LED |
| **J_LOOP** (8-pin) | Loop In button + LED, Loop Out button + LED, Reloop button + LED |
| **J_MASTER_TEMPO** (4-pin) | Master Tempo button + LED |
| **J_ENCODER_BACK** (7-pin) | Back/browse rotary encoder + Back button + LED |
| **J_TEMPO** (5-pin) | Tempo fader (analog ADC input) + Tempo Reset button + LED |
| **J_JOG** (4-pin) | Jog wheel quadrature encoder (JOG1/JOG2) |
| **J_TOUCH** (2-pin) | Jog wheel capacitive touch sensor |

**Summary of buttons:** Play, Cue, Loop In, Loop Out, Reloop, Master Tempo, Tempo Reset, Back — each with a corresponding LED. Plus a tempo fader (analog), a browse/back rotary encoder, a jog wheel (quadrature encoder), and a jog wheel touch sensor.

### Power

- **USB-C power input** with **CH224K** USB PD negotiation (requests 20V @ 3A = 60W, peak draw 42.4W)
- **SY8368AQQC** synchronous buck converter stepping 20V down to 5V for the CM5 and peripherals
- **AP2112K** 3.3V LDO for logic-level components
- **AP2553W** USB power switches for safe hot-plug on DJ USB ports

### Connectivity

| Port | Type | Speed | Purpose |
|---|---|---|---|
| **USB-A** | Full-size | USB 3.0 SuperSpeed | Rekordbox USB sticks |
| **USB-A** | Full-size | Power only (no data) | Auxiliary power for gadgets/lights |
| **USB-C** | Receptacle | USB 2.0 | General connectivity |
| **Micro HDMI** | Type D | HDMI 1.4 | 10" touchscreen display |
| **Micro HDMI** | Type D | HDMI 1.4 | Debug/secondary screen |
| **Ethernet** | RJ45 (via CM5) | Gigabit | Network connectivity |

Additional internal USB: USB 3.0 Type-A for the touchscreen's USB touch interface.

- **HD3SS3220** USB-C orientation mux and **HD3SS3212** USB 3.0 signal switch for proper USB-C handling on the DJ stick port

### Protection

- **USBLC6-2SC6** ESD protection on USB data lines
- **BZT52C3V3S** Zener diodes for voltage clamping

## Chassis

Designed in Fusion 360 with a custom top panel and bottom tray that fits the original CDJ form factor:

- `CDJ-Top-Panel_v17.f3d` — top panel with cutouts for the jog wheel, buttons, and connectors
- `CDJ-Bottom-Tray.f3d` — bottom enclosure

## Project Structure

```
CDJ-MainBoard/              KiCad PCB project
├── CDJ-MainBoard.kicad_sch     Root schematic
├── arduino_midi.kicad_sch      ATmega32U4 MIDI controller subsystem
├── audio_outputs.kicad_sch     DAC and audio output stage (RCA + headphones)
├── power.kicad_sch             Power supply (USB-C PD, buck, LDOs)
├── hdmi.kicad_sch              Dual micro HDMI outputs + Ethernet
├── usb_dj_ports.kicad_sch      USB ports (DJ stick, aux power, connectivity)
├── test_points.kicad_sch       Test points for debugging
└── CDJ-MainBoard.kicad_pcb     PCB layout

JLC2KiCad_lib/              Component library (symbols, footprints, 3D models)
kicad-thirdparty-footprints/    Third-party footprints (CM5, DAC, RCA jacks)
*.f3d                       Fusion 360 chassis models
*.net                       Exported netlists (various revisions)
```

## Software Stack

- **[Mixxx](https://mixxx.org/)** — open-source DJ software running on Raspberry Pi OS
- **Rekordbox USB support** — reads Rekordbox-exported USB sticks for seamless library access
- **MIDI mapping** — the ATmega32U4 presents as a standard USB MIDI device, so Mixxx sees the jog wheel, buttons, and fader as native MIDI controls

## Tools Used

- **KiCad** — schematic capture and PCB layout
- **Fusion 360** — mechanical design
- **JLC2KiCad** — importing JLCPCB component libraries into KiCad
- **JLCPCB** — PCB fabrication and assembly

## Schematic

![Full schematic](screenshots/Screenshot%202026-02-08%20at%2019.56.48.png)

## License

This is a personal hardware project. Feel free to use it as reference for your own builds.
