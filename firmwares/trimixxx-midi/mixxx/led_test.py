#!/usr/bin/env python3
"""LED-feedback test for the TriMixxx deck: drive the deck's LEDs over serial.

Sends the same Note-On/Note-Off bytes Mixxx would send back to the deck, but
straight down the serial line -- so it exercises the Pi(TX) -> S3(RX) wire and
the firmware's LED handling (onMidiFromMixxx) in isolation, with no Mixxx, no
ttymidi and no MIDI stack in the way. If an LED lights here, the return path is
good and any missing feedback in Mixxx is a Mixxx-output problem.

    uv run led_test.py --port /dev/serial0            # blink the PLAY LED
    uv run led_test.py --port /dev/serial0 --all      # sweep every LED

DECK_DEBUG must be 0 in the firmware (normal deck operation).
"""

# /// script
# requires-python = "==3.13.*"
# dependencies = ["pyserial==3.5"]
# ///
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import serial

# ---- MIDI + control map. Mirrors lib/PiLink/MidiMap.hpp -- keep in sync. ----
NOTE_ON = 0x90
VELOCITY = 0x7F
NOTE_PLAY = 0x3C
NOTE_CUE = 0x3D
NOTE_LOOP_IN = 0x3E
NOTE_LOOP_OUT = 0x3F
PAD_BASE = 0x00
PAD_COUNT = 50

# Named LEDs the firmware echoes (see onMidiFromMixxx in main.cpp). RELOOP (0x40),
# encoder push (0x41) and jog touch (0x42) drive no LED, so they are omitted.
NAMED_LEDS = {
    "play": NOTE_PLAY,
    "cue": NOTE_CUE,
    "loop_in": NOTE_LOOP_IN,
    "loop_out": NOTE_LOOP_OUT,
}


def led(ser: serial.Serial, note: int, *, on: bool) -> None:
    """Turn one LED on or off by sending its Note-On (velocity 0 = off)."""
    ser.write(bytes([NOTE_ON, note, VELOCITY if on else 0]))


def blink(
    ser: serial.Serial, note: int, *, times: int = 3, period: float = 0.4
) -> None:
    """Blink one LED a few times so a working RX path is unmistakable."""
    for _ in range(times):
        led(ser, note, on=True)
        time.sleep(period / 2)
        led(ser, note, on=False)
        time.sleep(period / 2)


def sweep(ser: serial.Serial) -> None:
    """Light every named LED in turn, then chase across the ring pads."""
    for name, note in NAMED_LEDS.items():
        print(f"  {name} (note {note:#04x})")
        led(ser, note, on=True)
        time.sleep(0.4)
        led(ser, note, on=False)
    print(f"  ring pads 0..{PAD_COUNT - 1}")
    for i in range(PAD_COUNT):
        led(ser, PAD_BASE + i, on=True)
        time.sleep(0.03)
        led(ser, PAD_BASE + i, on=False)


def find_port() -> str | None:
    """Return an auto-detected serial device, or None if none is found."""
    for pattern in ("cu.usbserial*", "ttyUSB*", "serial0"):
        ports = sorted(str(p) for p in Path("/dev").glob(pattern))
        if ports:
            return ports[0]
    return None


def parse_args() -> argparse.Namespace:
    """Parse the command-line options."""
    ap = argparse.ArgumentParser(description="LED-feedback test (TriMixxx).")
    ap.add_argument("--port", help="serial device (auto; /dev/serial0 on a Pi)")
    ap.add_argument("--baud", type=int, default=115200, help="must match PiLink baud")
    ap.add_argument("--all", action="store_true", help="sweep every LED, not just PLAY")
    return ap.parse_args()


def main() -> None:
    """Open the serial port and drive the deck LEDs."""
    args = parse_args()
    port = args.port or find_port()
    if not port:
        sys.exit("No serial port found. Pass --port (e.g. /dev/serial0 on a Pi).")
    with serial.Serial(port, args.baud) as ser:
        time.sleep(0.2)  # let the line settle before the first byte
        if args.all:
            print(f"Sweeping every LED on {port} @ {args.baud} ...")
            sweep(ser)
        else:
            print(f"Blinking the PLAY LED on {port} @ {args.baud} ...")
            blink(ser, NOTE_PLAY)
    print("done")


if __name__ == "__main__":
    main()
