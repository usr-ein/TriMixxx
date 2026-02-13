# TriMixxx

A custom CDJ (Compact Disc Jockey) unit built from scratch around a Raspberry Pi CM5, designed to run [Mixxx](https://mixxx.org/) DJ software. Features a custom PCB, a custom 3D-printed chassis, and reuses original CDJ buttons and jog wheel for an authentic DJ experience.

![Full schematic](screenshots/Screenshot%202026-02-08%20at%2019.56.48.png)

## What is this?

TriMixxx replaces the internals of a CDJ with modern, open-source-friendly hardware while keeping the physical controls that DJs know and love. Plug in a Rekordbox-formatted USB stick, and you're ready to mix — no laptop required.

## Hardware Architecture

### Compute

- **Raspberry Pi CM5** — runs Mixxx and handles audio playback, connected via dual DF40 100-pin high-density connectors

### Audio

- **TI PCM5242** — high-quality stereo DAC connected over I2S for low-latency audio output
- **Dual audio outputs** — 6.35mm (1/4") and 3.5mm headphone jacks

### MIDI Controller

- **ATmega32U4** — acts as a USB MIDI class-compliant device, reading the original CDJ's buttons, encoders, and jog wheel and translating them into MIDI messages for Mixxx
- **16 MHz crystal** oscillator for reliable USB timing

### Power

- **USB-C power input** with **CH224K** USB PD negotiation (requests 20V)
- **TPS54560** buck converter stepping 20V down to 5V for the CM5 and peripherals
- **AP2112K** 3.3V LDO for logic-level components
- **AP2553W** USB power switches for safe hot-plug on DJ USB ports

### Connectivity

- **USB 3.0 Type-A port** — for Rekordbox USB sticks
- **HDMI output** — for an optional display (waveforms, library browsing)
- **USB-C** — power delivery and data
- **HD3SS3220** USB-C orientation mux and **HD3SS3212** USB 3.0 signal switch for proper USB-C handling
- **Ethernet** (via CM5) for network connectivity

### Protection

- **USBLC6-2SC6** ESD protection on USB data lines
- **BZT52C3V3S** Zener diodes for voltage clamping

## Chassis

Designed in Fusion 360 with a custom top panel and bottom tray that fits the original CDJ form factor:

- `CDJ-Top-Panel_v17.f3d` — top panel with cutouts for the jog wheel, buttons, and connectors
- `CDJ-Bottom-Tray.f3d` — bottom enclosure

## Project Structure

```
CDJ-MainBoard/              KiCad PCB project (6-layer board)
├── CDJ-MainBoard.kicad_sch     Root schematic
├── arduino_midi.kicad_sch      ATmega32U4 MIDI controller subsystem
├── audio_outputs.kicad_sch     DAC and audio output stage
├── power.kicad_sch             Power supply (USB-C PD, buck, LDOs)
├── hdmi.kicad_sch              HDMI output
├── usb_dj_ports.kicad_sch      USB 3.0 host port for DJ USB sticks
├── test_points.kicad_sch       Test points for debugging
└── CDJ-MainBoard.kicad_pcb     PCB layout

JLC2KiCad_lib/              Component library (symbols, footprints, 3D models)
kicad-thirdparty-footprints/    Third-party footprints (CM5, DAC, connectors)
*.f3d                       Fusion 360 chassis models
*.net                       Exported netlists (various revisions)
```

## Software Stack

- **[Mixxx](https://mixxx.org/)** — open-source DJ software running on Raspberry Pi OS
- **Rekordbox USB support** — reads Rekordbox-exported USB sticks for seamless library access
- **MIDI mapping** — the ATmega32U4 presents as a standard USB MIDI device, so Mixxx sees the jog wheel, buttons, and knobs as native MIDI controls

## Tools Used

- **KiCad** — schematic capture and PCB layout
- **Fusion 360** — mechanical design
- **JLC2KiCad** — importing JLCPCB component libraries into KiCad
- **JLCPCB** — PCB fabrication and assembly

## License

This is a personal hardware project. Feel free to use it as reference for your own builds.
