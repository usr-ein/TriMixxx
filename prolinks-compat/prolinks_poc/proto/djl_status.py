"""CDJ status packets — UDP port 50002, type ``0x0a``.

Sent roughly every 200 ms by each player, **unicast to peers**, which is why
they are invisible to a tap that only sees broadcast — the mistake behind the
retracted F15.

Implemented from ``research/03``. Decode-only for now: we need to *read* what
players say about their media and playback, and synthesising a convincing
status packet is a separate problem (it is what makes a real deck offer us as a
LINK source, and it needs its own capture work).

**The header differs from the keep-alive layout on port 50000.** Here the
device name occupies ``0x0b``–``0x1f`` — 21 bytes, starting one byte earlier —
and the device number sits at ``0x21`` rather than in the payload. Reusing the
50000 decoder here yields plausible-looking nonsense rather than an error, so
the two are deliberately kept apart.
"""

from __future__ import annotations

import enum
from dataclasses import dataclass

from .bytes import ByteReader
from .djl import MAGIC, MAGIC_LEN
from .errors import DecodeError

__all__ = [
    "STATUS_PORT",
    "StatusType",
    "MediaState",
    "PlayState",
    "CdjStatus",
    "decode_status",
    "is_status_packet",
]

STATUS_PORT = 50002

#: Shortest packet we will attempt. prolink-connect ignores anything under
#: 0xc8 as a truncated rekordbox status; the media fields we care about live
#: below 0x76, so this is the real floor for us.
MIN_STATUS_LENGTH = 0x76


class StatusType(enum.IntEnum):
    CDJ_STATUS = 0x0A
    MIXER_STATUS = 0x29
    MEDIA_QUERY = 0x05
    MEDIA_RESPONSE = 0x06


class MediaState(enum.IntEnum):
    """A slot's local state (``research/03`` §1, offsets ``0x6f``/``0x73``)."""

    LOADED = 0x00
    UNMOUNTING = 0x02
    UNMOUNTING_ALT = 0x03
    EMPTY = 0x04

    @property
    def has_media(self) -> bool:
        return self is MediaState.LOADED


@dataclass(frozen=True)
class CdjStatus:
    """The subset of a status packet we can currently read with confidence.

    Deliberately partial. ``research/03`` documents many more fields, but the
    ones below are what answer the questions in front of us -- which slots hold
    media, what is loaded, and whether the deck is playing -- and each is marked
    *confirmed* in the source rather than inferred.
    """

    device_number: int
    name: str
    subtype: int
    wire_length: int

    #: Where the currently loaded track came from.
    source_player: int
    source_slot: int
    track_type: int
    track_id: int

    #: Local media state per slot.
    usb_state: int
    sd_state: int
    #: ``0x75``: set when any media is available anywhere on the network.
    link_available: int

    play_state: int
    bpm_100: int

    @property
    def usb(self) -> MediaState | int:
        try:
            return MediaState(self.usb_state)
        except ValueError:
            return self.usb_state

    @property
    def sd(self) -> MediaState | int:
        try:
            return MediaState(self.sd_state)
        except ValueError:
            return self.sd_state

    @property
    def has_usb(self) -> bool:
        return self.usb_state == MediaState.LOADED

    @property
    def has_sd(self) -> bool:
        return self.sd_state == MediaState.LOADED

    @property
    def bpm(self) -> float:
        return self.bpm_100 / 100.0

    def media_summary(self) -> str:
        def label(state: int) -> str:
            try:
                return MediaState(state).name.lower()
            except ValueError:
                return f"0x{state:02x}"

        return f"usb={label(self.usb_state)} sd={label(self.sd_state)}"

    def __str__(self) -> str:
        loaded = (
            f" loaded=player{self.source_player}/slot{self.source_slot}#{self.track_id}"
            if self.track_id
            else ""
        )
        return (
            f"D={self.device_number} {self.media_summary()} "
            f"link={self.link_available}{loaded}"
        )


def is_status_packet(data: bytes) -> bool:
    return (
        len(data) > MAGIC_LEN
        and data[:MAGIC_LEN] == MAGIC
        and data[MAGIC_LEN] == StatusType.CDJ_STATUS
    )


def decode_status(data: bytes) -> CdjStatus:
    """Decode a type-``0x0a`` CDJ status packet.

    Raises :class:`DecodeError` for anything that is not one, or is too short
    to carry the media fields. Longer packets from newer hardware decode fine:
    every offset used here is well inside even the shortest (pre-nexus, 0xd0)
    variant.
    """
    if not is_status_packet(data):
        raise DecodeError("not a CDJ status packet (type 0x0a on port 50002)")
    if len(data) < MIN_STATUS_LENGTH:
        raise DecodeError(
            f"status packet is {len(data)}B; need {MIN_STATUS_LENGTH}B for the media fields"
        )

    reader = ByteReader(data)
    # Name is 21 bytes from 0x0b here, not 20 from 0x0c as on port 50000.
    name = reader.raw_at(0x0B, 21).split(b"\x00", 1)[0].decode("ascii", errors="replace")

    return CdjStatus(
        device_number=reader.u8_at(0x21),
        name=name,
        subtype=reader.u8_at(0x20),
        wire_length=len(data),
        source_player=reader.u8_at(0x28),
        source_slot=reader.u8_at(0x29),
        track_type=reader.u8_at(0x2A),
        track_id=int.from_bytes(reader.raw_at(0x2C, 4), "big"),
        usb_state=reader.u8_at(0x6F),
        sd_state=reader.u8_at(0x73),
        link_available=reader.u8_at(0x75),
        play_state=reader.u8_at(0x7B),
        bpm_100=int.from_bytes(reader.raw_at(0x92, 2), "big") if len(data) > 0x93 else 0,
    )
