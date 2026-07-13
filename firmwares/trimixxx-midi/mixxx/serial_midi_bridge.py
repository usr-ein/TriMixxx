#!/usr/bin/env python3
"""Serial <-> MIDI bridge / monitor for the TriMixxx deck (Mac or Pi).

The S3 firmware sends raw MIDI bytes over UART0 at 115200 baud (the same stream
ttymidi consumes on the Pi). Two modes:

- default (bridge): re-emit on a *virtual MIDI port* (CoreMIDI on macOS, ALSA on
  Linux) so Mixxx sees the deck as a normal MIDI device; bidirectional, so LED
  feedback flows back to the deck. Needs python-rtmidi.
- --monitor: just decode + print each message. Needs only pyserial, so it runs
  anywhere with no native build -- handy for a quick check over SSH on the Pi.

    uv run serial_midi_bridge.py --debug                          # bridge (Mac)
    python3 serial_midi_bridge.py --monitor --port /dev/serial0   # monitor (Pi)

In Mixxx: Preferences -> Controllers -> "TriMixxx" -> load TriMixxx.midi.xml.
"""

# /// script
# requires-python = "==3.13.*"
# dependencies = ["pyserial==3.5", "python-rtmidi==1.5.8"]
# ///
# NOTE: bridge mode needs python-rtmidi. If uv builds it from source (no wheel
# for your Python): macOS needs Xcode CLT; Debian/Pi needs
# `sudo apt install python3-dev libasound2-dev`. --monitor needs only pyserial.
from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import TYPE_CHECKING

import serial

if TYPE_CHECKING:
    from collections.abc import Iterator

# ---- MIDI status bytes (channel 1 -> low nibble 0) ----
NOTE_ON = 0x90
NOTE_OFF = 0x80
CONTROL_CHANGE = 0xB0
PROGRAM_CHANGE = 0xC0
CHANNEL_PRESSURE = 0xD0
REALTIME_MIN = 0xF8  # 0xF8..0xFF are single-byte real-time messages
STATUS_BIT = 0x80
TYPE_MASK = 0xF0
MSG_LEN = 3  # note on/off and CC are 3 bytes
SIGN_WRAP = 0x80  # 7-bit two's complement: values >= HALF are negative
HALF = 0x40

# ---- Control map. Mirrors lib/PiLink/MidiMap.hpp -- keep in sync. ----
PAD_COUNT = 50  # ring pads = notes 0 .. PAD_COUNT-1
CC_ENCODER = 0x10
CC_JOG = 0x11
CC_TEMPO_MSB = 0x12
CC_TEMPO_LSB = 0x32
TEMPO_MAX = 0x3FFF  # 14-bit full scale
NOTE_NAMES = {
    60: "PLAY",
    61: "CUE",
    62: "LOOP_IN",
    63: "LOOP_OUT",
    64: "RELOOP",
    65: "ENC_SW",
    66: "JOG_TOUCH",
}
CC_NAMES = {
    CC_ENCODER: "ENCODER",
    CC_JOG: "JOG",
    CC_TEMPO_MSB: "TEMPO_MSB",
    CC_TEMPO_LSB: "TEMPO_LSB",
}

# Remembers the last tempo MSB so the LSB can be shown as one 14-bit value.
_tempo_state = {"msb": 0}


def data_bytes(status: int) -> int:
    """Return the number of data bytes that follow a MIDI status byte."""
    if status & TYPE_MASK in (PROGRAM_CHANGE, CHANNEL_PRESSURE):
        return 1
    return 2  # note on/off, CC, pitch bend, poly pressure


def note_name(note: int) -> str:
    """Return the control name for a note number (ring pad or named button)."""
    if 0 <= note < PAD_COUNT:
        return f"PAD[{note}]"
    return NOTE_NAMES.get(note, f"note{note}")


def _decode_cc(cc: int, value: int) -> str:
    """Decode a control-change message to text."""
    if cc in (CC_ENCODER, CC_JOG):  # relative: 7-bit two's complement
        delta = value if value < HALF else value - SIGN_WRAP
        return f"CC      {CC_NAMES[cc]} delta={delta:+d}"
    if cc == CC_TEMPO_MSB:
        _tempo_state["msb"] = value
        return f"CC      TEMPO_MSB msb={value}"
    if cc == CC_TEMPO_LSB:  # combine with the last MSB into the 14-bit value
        val = (_tempo_state["msb"] << 7) | value
        pct = round(val * 100 / TEMPO_MAX)
        return f"CC      TEMPO_LSB -> 14bit={val} ({pct}%)"
    return f"CC      {CC_NAMES.get(cc, f'CC{cc}')} val={value}"


def decode(msg: list[int]) -> str:
    """Turn a raw MIDI message into e.g. 'NoteOn PLAY vel=127'."""
    if len(msg) != MSG_LEN:
        return "?"
    kind = msg[0] & TYPE_MASK
    if kind == NOTE_ON:
        return f"NoteOn  {note_name(msg[1])} vel={msg[2]}"
    if kind == NOTE_OFF:
        return f"NoteOff {note_name(msg[1])}"
    if kind == CONTROL_CHANGE:
        return _decode_cc(msg[1], msg[2])
    return "?"


def fmt(msg: list[int]) -> str:
    """Format a message as hex bytes plus its decoded meaning."""
    hexs = " ".join(f"{b:02X}" for b in msg)
    return f"{hexs:<11} {decode(msg)}"


def read_messages(ser: serial.Serial) -> Iterator[list[int]]:
    """Yield complete MIDI messages parsed from the serial stream."""
    status = 0
    buf: list[int] = []
    while True:
        chunk = ser.read(1)
        if not chunk:
            continue
        byte = chunk[0]
        if byte >= REALTIME_MIN:  # single-byte real-time, ignore
            continue
        if byte & STATUS_BIT:  # status byte
            status = byte
            buf = [byte]
        elif status:  # data byte
            buf.append(byte)
            if len(buf) == 1 + data_bytes(status):
                yield buf
                buf = [status]  # running status


def find_port() -> str | None:
    """Return an auto-detected serial device, or None if none is found.

    Tries a USB-serial adapter (macOS / Linux) then the Pi's GPIO UART.
    """
    for pattern in ("cu.usbserial*", "ttyUSB*", "serial0"):
        ports = sorted(str(p) for p in Path("/dev").glob(pattern))
        if ports:
            return ports[0]
    return None


def parse_args() -> argparse.Namespace:
    """Parse the command-line options."""
    ap = argparse.ArgumentParser(description="Serial<->MIDI bridge (TriMixxx).")
    ap.add_argument("--port", help="serial device (auto; /dev/serial0 on a Pi)")
    ap.add_argument("--baud", type=int, default=115200, help="must match PiLink baud")
    ap.add_argument("--name", default="TriMixxx", help="virtual MIDI port name")
    ap.add_argument("--debug", action="store_true", help="print each bridged message")
    ap.add_argument(
        "--monitor",
        action="store_true",
        help="just print decoded MIDI; no virtual port (needs only pyserial)",
    )
    return ap.parse_args()


def run_bridge(ser: serial.Serial, args: argparse.Namespace) -> None:
    """Bridge the serial stream to a virtual MIDI port, both directions."""
    import rtmidi  # noqa: PLC0415 -- optional dep; only the bridge needs it

    midi_out = rtmidi.MidiOut()  # deck -> host (virtual MIDI source)
    midi_out.open_virtual_port(args.name)
    midi_in = rtmidi.MidiIn()  # host -> deck (virtual MIDI destination)

    def from_host(event: tuple[list[int], float], _: object = None) -> None:
        msg = event[0]
        ser.write(bytes(msg))
        if args.debug:
            print(f"host->deck {fmt(msg)}")

    midi_in.set_callback(from_host)
    midi_in.open_virtual_port(args.name)

    print(f"Bridging {ser.port} @ {ser.baudrate}  <->  virtual MIDI port '{args.name}'")
    print("Touch a control on the deck (DECK_DEBUG must be 0). Ctrl-C to stop.")
    for msg in read_messages(ser):
        midi_out.send_message(msg)
        if args.debug:
            print(f"deck->host {fmt(msg)}")


def main() -> None:
    """Open the serial port, then monitor it or bridge it to virtual MIDI."""
    args = parse_args()
    port = args.port or find_port()
    if not port:
        sys.exit("No serial port found. Pass --port (e.g. /dev/serial0 on a Pi).")
    ser = serial.Serial(port, args.baud, timeout=0.05)
    try:
        if args.monitor:
            print(f"Monitoring {port} @ {args.baud} (decode only). Ctrl-C to stop.")
            for msg in read_messages(ser):
                print(fmt(msg))
        else:
            run_bridge(ser, args)
    except KeyboardInterrupt:
        print("\nbye")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
