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
    "build_status",
    "MediaQuery",
    "decode_media_query",
    "build_media_response",
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


# -- emitting status (announced mode) --------------------------------------
#
# A status packet is 284 bytes of which, across 749 consecutive packets from an
# idle CDJ-2000nexus, only **six** ever changed: the USB slot state, the
# link-available flag, two still-unidentified bytes at 0x6a/0x74, and the
# 16-bit packet counter. So rather than construct one field by field from a
# specification full of unknowns, we start from a real packet and substitute
# the fields we understand. Everything we cannot name is preserved exactly as
# a real deck sends it.
#
# Captured from the author's deck A (firmware 1.44) with a stick loaded. The
# device name, device number, media state, link flag and packet counter are
# zeroed here, so nothing identifying the source deck is baked in.

_TEMPLATE = bytes.fromhex(
    "5173707431576d4a4f4c0a000000000000000000000000000000000000000001040000f8"
    "000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000001000604"
    "00000000000000000100000000000000312e343400000000000000030084fffe000f8312"
    "7fffffff7fffffff00000000000000ff0000000401ff0000000000000000000000000000"
    "000001000000000000000000000f831200000000000000000f0100001234567800000001"
    "010101010201000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000001500000753000005b4"
)

#: Offsets we substitute. Everything else in the template is left untouched.
OFF_NAME = 0x0B
#: 20 bytes, not the 21 ``research/03`` §0 states. Byte 0x1f is a constant
#: 0x01 in all 1503 captured packets -- the same shape as the keep-alive, where
#: the name is 0x0c-0x1f and the constant sits at 0x20. FINDINGS C14.
LEN_NAME_STATUS = 20
OFF_SUBTYPE = 0x20
OFF_DEVICE = 0x21
OFF_DEVICE_2 = 0x24
OFF_USB_STATE = 0x6F
OFF_SD_STATE = 0x73
OFF_LINK = 0x75
OFF_PLAY_STATE = 0x7B
OFF_FIRMWARE = 0x7C
OFF_PACKET_COUNTER = 0xC8


def build_status(
    device_number: int,
    name: str = "CDJ-2000nexus",
    usb_state: int = MediaState.LOADED,
    sd_state: int = MediaState.EMPTY,
    link_available: int = 1,
    play_state: int = 0x00,
    firmware: str = "1.44",
    packet_counter: int = 0,
) -> bytes:
    """Synthesise a CDJ status packet for UDP 50002.

    This is what makes a real player treat us as a deck with media in it.
    Keep-alives on 50000 only announce that we exist; media presence is
    advertised here (FINDINGS F20), and these packets are unicast to announced
    peers (F21) -- so a device that never sends them looks, to a CDJ, like a
    player with empty slots.

    ``packet_counter`` occupies ``0xc8``-``0xcb`` and increments once per
    packet on real hardware. It is passed in rather than held as module state
    so that emission stays a pure function.
    """
    packet = bytearray(_TEMPLATE)
    encoded = name.encode("ascii", errors="replace")[:LEN_NAME_STATUS]
    packet[OFF_NAME : OFF_NAME + LEN_NAME_STATUS] = encoded.ljust(LEN_NAME_STATUS, b"\x00")
    packet[OFF_DEVICE] = device_number & 0xFF
    # The device number appears twice; prolink-connect notes the same, and a
    # real packet carries it at both 0x21 and 0x24.
    packet[OFF_DEVICE_2] = device_number & 0xFF
    packet[OFF_USB_STATE] = usb_state & 0xFF
    packet[OFF_SD_STATE] = sd_state & 0xFF
    # 0x74 is left exactly as the real deck sent it. It takes 0 and 1 and is
    # clearly media-related, but it does not track 0x75: three of the four
    # combinations occur, so it is a separate flag we cannot yet name. Guessing
    # would be worse than copying.
    packet[OFF_LINK] = link_available & 0xFF
    packet[OFF_PLAY_STATE] = play_state & 0xFF
    packet[OFF_FIRMWARE : OFF_FIRMWARE + 4] = firmware.encode("ascii")[:4].ljust(4, b"\x00")
    packet[OFF_PACKET_COUNTER : OFF_PACKET_COUNTER + 4] = (
        packet_counter & 0xFFFFFFFF
    ).to_bytes(4, "big")
    return bytes(packet)


# -- media query / response (types 0x05 / 0x06) ----------------------------
#
# A player asks its peers "device N, what is in slot S?" with a type-0x05
# packet on 50002, and expects a type-0x06 reply describing the medium. Until
# we answered these, a deck that had otherwise fully accepted us -- it was
# unicasting status to us, and had completed a portmap + MNT against our NFS
# server -- still refused to list us as a LINK source, because as far as it
# knew our slots held nothing.
#
# Template taken from a real reply; the identifying and per-medium fields are
# zeroed and substituted.

_MEDIA_RESPONSE_TEMPLATE = bytes.fromhex(
    "5173707431576d4a4f4c060000000000000000000000000000000000000000010000009c"
    "000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000"
    "0032003000320035002d00300036002d0032003400000000003100300030003000000000"
    "000000000000000000000000000000000000000000000000000001010000000000000007"
    "28ca8000000000057d06c000"
)

OFF_MR_DEVICE = 0x24
OFF_MR_SLOT = 0x28
OFF_MR_NAME = 0x2C
LEN_MR_NAME = 0x40
OFF_MR_TRACK_COUNT = 0xA4
OFF_MR_PLAYLIST_COUNT = 0xAC


@dataclass(frozen=True)
class MediaQuery:
    """A type-``0x05`` request: "device *target*, describe slot *slot*"."""

    requester: int
    requester_ip: str
    target_device: int
    slot: int


def decode_media_query(data: bytes) -> MediaQuery:
    """Decode a media query. Raises :class:`DecodeError` if it is not one."""
    if len(data) < 0x30 or data[:MAGIC_LEN] != MAGIC or data[MAGIC_LEN] != StatusType.MEDIA_QUERY:
        raise DecodeError("not a media query (type 0x05 on port 50002)")
    reader = ByteReader(data)
    return MediaQuery(
        requester=reader.u8_at(0x21),
        requester_ip=".".join(str(b) for b in reader.raw_at(0x24, 4)),
        target_device=int.from_bytes(reader.raw_at(0x28, 4), "big"),
        slot=int.from_bytes(reader.raw_at(0x2C, 4), "big"),
    )


def build_media_response(
    device_number: int,
    slot: int,
    media_name: str,
    track_count: int,
    playlist_count: int,
    name: str = "CDJ-2000nexus",
) -> bytes:
    """Describe one of our media slots, in reply to a query.

    The counts are what the player shows in its Link Info panel, so they should
    be the real ones -- a deck that is told there are no tracks has no reason
    to offer the medium for browsing.

    The media name is UTF-16 **big**-endian here, like the dbserver strings and
    unlike the UTF-16LE of the NFS layer.
    """
    packet = bytearray(_MEDIA_RESPONSE_TEMPLATE)
    encoded = name.encode("ascii", errors="replace")[:20]
    packet[0x0B:0x1F] = encoded.ljust(20, b"\x00")
    packet[0x21] = device_number & 0xFF
    packet[OFF_MR_DEVICE : OFF_MR_DEVICE + 4] = (device_number & 0xFFFFFFFF).to_bytes(4, "big")
    packet[OFF_MR_SLOT : OFF_MR_SLOT + 4] = (slot & 0xFFFFFFFF).to_bytes(4, "big")

    volume = media_name.encode("utf-16-be")[:LEN_MR_NAME]
    packet[OFF_MR_NAME : OFF_MR_NAME + LEN_MR_NAME] = volume.ljust(LEN_MR_NAME, b"\x00")

    packet[OFF_MR_TRACK_COUNT : OFF_MR_TRACK_COUNT + 4] = (
        track_count & 0xFFFFFFFF
    ).to_bytes(4, "big")
    packet[OFF_MR_PLAYLIST_COUNT : OFF_MR_PLAYLIST_COUNT + 4] = (
        playlist_count & 0xFFFFFFFF
    ).to_bytes(4, "big")
    return bytes(packet)
