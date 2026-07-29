"""DJ-Link discovery / keep-alive codec — UDP port 50000.

Implemented from ``research/02-device-discovery-and-keepalive.md``, whose field
tables use **dysentery byte numbering**. Offsets below are quoted in hex to
match that document so the code can be diffed against it line by line.

Both directions are implemented for every packet type, even the ones we never
send in the passive-first configuration. That is deliberate: the encoders are
what the virtual CDJ (M9) and eventually the serve side need, and having them
now means every decoder gets a free round-trip test (``tests/test_djl.py``).

Common header, shared by every packet kind (``research/02`` §0)::

    00-09  magic "Qspt1WmJOL"
    0a     packet type          <- the discriminator
    0b     subtype              normally 00; 01 marks a directed reply
    0c-1f  device name          20 bytes ASCII, NUL-padded
    20     constant 01
    21     device kind          02 CDJ / 01 mixer / 03 rekordbox+CDJ-3000
    22     padding 00
    23     stype                pairs with the type byte
    24+    type-specific payload
"""

from __future__ import annotations

import enum
import socket
from dataclasses import dataclass

from .bytes import ByteReader, ByteWriter
from .errors import DecodeError

__all__ = [
    "MAGIC",
    "DISCOVERY_PORT",
    "BEAT_PORT",
    "STATUS_PORT",
    "KEEPALIVE_INTERVAL_S",
    "DISCOVERY_INTERVAL_S",
    "DEVICE_TIMEOUT_S",
    "PacketType",
    "DeviceKind",
    "AssignmentMode",
    "DjlPacket",
    "Hello",
    "ClaimMac",
    "ClaimIp",
    "ClaimNumber",
    "KeepAlive",
    "NumberConflict",
    "UnknownPacket",
    "decode",
    "is_djl_packet",
    "format_mac",
]

# -- wire constants (research/02 §0) --------------------------------------

#: The 10-byte header every DJ-Link datagram starts with, on all three ports.
MAGIC = b"Qspt1WmJOL"
MAGIC_LEN = 10

DISCOVERY_PORT = 50000
BEAT_PORT = 50001
STATUS_PORT = 50002

#: Steady-state keep-alive cadence. research/02 §0: confirmed 1.5 s.
KEEPALIVE_INTERVAL_S = 1.5
#: Cadence of the startup handshake packets. research/02 §1.0.
DISCOVERY_INTERVAL_S = 0.3
#: How long after the last keep-alive a peer is considered gone.
#: ~6-7 missed keep-alives; research/02 §2.
DEVICE_TIMEOUT_S = 10.0

# Header field offsets.
OFF_TYPE = 0x0A
OFF_SUBTYPE = 0x0B
OFF_NAME = 0x0C
LEN_NAME = 20
OFF_CONST_ONE = 0x20
OFF_DEVICE_KIND = 0x21
OFF_PAD = 0x22
OFF_STYPE = 0x23
#: First byte of the type-specific payload; also the common-header length.
HEADER_LEN = 0x24


class PacketType(enum.IntEnum):
    """Byte ``0a``, the discriminator (``research/02`` §0.1).

    Note the values are not ordered by handshake position: the auto-assign
    chain is HELLO(0a) -> CLAIM_MAC(00) -> CLAIM_IP(02) -> CLAIM_NUMBER(04) ->
    KEEP_ALIVE(06), with NUMBER_CONFLICT(08) as the interrupt.
    """

    CLAIM_MAC = 0x00  # stage 1: publish MAC
    MIXER_ASSIGN_INTENT = 0x01  # mixer -> player, channel-specific port only
    CLAIM_IP = 0x02  # stage 2: publish IP, propose a number ("IdUseRequest")
    MIXER_ASSIGN = 0x03  # mixer -> player: "use device number D"
    CLAIM_NUMBER = 0x04  # stage 3: assert the number
    MIXER_ASSIGN_DONE = 0x05  # mixer -> player: assignment finished
    KEEP_ALIVE = 0x06  # steady state, every ~1.5 s
    NUMBER_CONFLICT = 0x08  # "that number is mine" -- unicast by the owner
    HELLO = 0x0A  # initial announcement


class DeviceKind(enum.IntEnum):
    """Byte ``21`` (``research/02`` §0.2). Critical for impersonation."""

    MIXER = 0x01
    CDJ = 0x02
    #: rekordbox desktop, and also CDJ-3000.
    REKORDBOX_OR_CDJ3000 = 0x03
    #: Seen only in the CDJ-3000 hello (research/02 §1.6).
    CDJ3000_HELLO = 0x04


class AssignmentMode(enum.IntEnum):
    """Byte ``31`` of the stage-2 claim (``research/02`` §1.3)."""

    AUTO = 0x01
    MANUAL = 0x02


# Per-type subtype byte and total packet length (research/02 §0.1).
#
# The stype byte equals the total packet length for every type. research/02
# §0.1 lists CLAIM_NUMBER as stype 0x26 but length 0x2a, which would leave four
# undescribed trailing bytes -- but six real type-0x04 packets in the dysentery
# captures are all 0x26 bytes long, so the table's length column is simply
# wrong there. See FINDINGS.md, correction C2.
_STYPE = {
    PacketType.HELLO: 0x25,
    PacketType.CLAIM_MAC: 0x2C,
    PacketType.CLAIM_IP: 0x32,
    PacketType.CLAIM_NUMBER: 0x26,
    PacketType.KEEP_ALIVE: 0x36,
    PacketType.NUMBER_CONFLICT: 0x29,
}
_LENGTH = dict(_STYPE)


def default_role(device_kind: int) -> int:
    """The ``01`` CDJ / ``02`` mixer role byte that recurs across packet types.

    Appears at offset ``30`` in the stage-2 claim and at ``34`` in the
    keep-alive, and tracks the device kind in both.
    """
    return 0x02 if device_kind == DeviceKind.MIXER else 0x01


def format_mac(mac: bytes) -> str:
    return ":".join(f"{b:02x}" for b in mac)


def _ip_to_bytes(dotted: str) -> bytes:
    return socket.inet_aton(dotted)


def _ip_from_bytes(raw: bytes) -> str:
    return socket.inet_ntoa(raw)


# -- packets ---------------------------------------------------------------


@dataclass(frozen=True, kw_only=True)
class DjlPacket:
    """Fields common to every UDP-50000 packet.

    ``name_raw`` keeps the literal 20 bytes alongside the decoded ``name``.
    Milestone M1 exists partly to capture those bytes: the exact casing of
    ``CDJ-2000nexus`` is *inferred* in ``research/02`` §4.1 and has never been
    seen in a published capture, so the decoded string is not good enough.

    ``wire_length`` records the datagram's actual size rather than the
    documented one, so ``spec/gen_spec.py`` can report the distinct sizes we
    really observed per type. Decoders validate that enough bytes are present
    to read their named fields, but do not insist on the documented total --
    generation variants differ (the CDJ-3000 hello is one byte longer).
    """

    name: str
    name_raw: bytes
    device_kind: int
    subtype: int = 0x00
    stype: int = 0x00
    wire_length: int = 0

    #: Overridden by each subclass. Deliberately unannotated so that
    #: ``dataclass`` treats it as a class attribute rather than a field.
    PACKET_TYPE = None

    @property
    def packet_type(self) -> int | None:
        return self.PACKET_TYPE

    def encode(self) -> bytes:  # pragma: no cover - overridden
        raise NotImplementedError


def _write_header(
    packet_type: int, name: str, device_kind: int, total_length: int, subtype: int = 0x00
) -> ByteWriter:
    """Build the 0x24-byte common header inside a zeroed *total_length* buffer."""
    writer = ByteWriter().allocate(total_length)
    writer.put_at(0x00, MAGIC)
    writer.u8_at(OFF_TYPE, packet_type)
    writer.u8_at(OFF_SUBTYPE, subtype)
    writer.put_at(OFF_NAME, ByteWriter().ascii_fixed(name, LEN_NAME).data())
    writer.u8_at(OFF_CONST_ONE, 0x01)
    writer.u8_at(OFF_DEVICE_KIND, device_kind)
    writer.u8_at(OFF_PAD, 0x00)
    writer.u8_at(OFF_STYPE, _STYPE.get(packet_type, total_length))
    return writer


def _common_fields(reader: ByteReader, data: bytes) -> dict:
    name_raw = reader.raw_at(OFF_NAME, LEN_NAME)
    return {
        "name": name_raw.split(b"\x00", 1)[0].decode("ascii", errors="replace"),
        "name_raw": name_raw,
        "device_kind": reader.u8_at(OFF_DEVICE_KIND),
        "subtype": reader.u8_at(OFF_SUBTYPE),
        "stype": reader.u8_at(OFF_STYPE),
        "wire_length": len(data),
    }


@dataclass(frozen=True, kw_only=True)
class Hello(DjlPacket):
    """Type ``0a`` — initial announcement (``research/02`` §1.1).

    Broadcast ~3 times, ~300 ms apart. The only difference between the CDJ and
    mixer forms is the single payload byte: ``01`` CDJ, ``02`` mixer (a
    DJM-900NXS has been seen sending ``03``).
    """

    PACKET_TYPE = PacketType.HELLO

    payload: int = 0x01

    def encode(self) -> bytes:
        length = _LENGTH[PacketType.HELLO]
        writer = _write_header(PacketType.HELLO, self.name, self.device_kind, length)
        writer.u8_at(0x24, self.payload)
        return writer.data()


@dataclass(frozen=True, kw_only=True)
class ClaimMac(DjlPacket):
    """Type ``00`` — stage-1 claim, publishes the MAC (``research/02`` §1.2)."""

    PACKET_TYPE = PacketType.CLAIM_MAC

    iteration: int
    flags: int
    mac: bytes

    def encode(self) -> bytes:
        length = _LENGTH[PacketType.CLAIM_MAC]
        writer = _write_header(PacketType.CLAIM_MAC, self.name, self.device_kind, length)
        writer.u8_at(0x24, self.iteration)
        writer.u8_at(0x25, self.flags)
        writer.put_at(0x26, self.mac)
        return writer.data()


@dataclass(frozen=True, kw_only=True)
class ClaimIp(DjlPacket):
    """Type ``02`` — stage-2 claim / "IdUseRequest" (``research/02`` §1.3).

    Publishes our IP and MAC and *proposes* a device number. ``device_number``
    is ``0`` when asking a mixer to assign one (the channel-specific port flow,
    §1.7), in which case ``subtype`` is ``01`` to mark a directed reply.
    """

    PACKET_TYPE = PacketType.CLAIM_IP

    ip: str
    mac: bytes
    device_number: int
    iteration: int
    assignment_mode: int = AssignmentMode.AUTO
    #: Byte ``30``. research/02 §1.3 calls this "const 01", but a real
    #: DJM-2000nexus sends ``02`` here while a CDJ-2000nexus sends ``01`` --
    #: so it is the same CDJ/mixer role byte that appears elsewhere, not a
    #: constant. See FINDINGS.md, correction C1. ``None`` derives it.
    role: int | None = None

    def encode(self) -> bytes:
        length = _LENGTH[PacketType.CLAIM_IP]
        writer = _write_header(
            PacketType.CLAIM_IP, self.name, self.device_kind, length, self.subtype
        )
        writer.put_at(0x24, _ip_to_bytes(self.ip))
        writer.put_at(0x28, self.mac)
        writer.u8_at(0x2E, self.device_number)
        writer.u8_at(0x2F, self.iteration)
        writer.u8_at(
            0x30, self.role if self.role is not None else default_role(self.device_kind)
        )
        writer.u8_at(0x31, self.assignment_mode)
        return writer.data()


@dataclass(frozen=True, kw_only=True)
class ClaimNumber(DjlPacket):
    """Type ``04`` — stage-3 claim, asserts the number (``research/02`` §1.4).

    Auto-assign sends three of these (N=1,2,3); a manually configured number
    sends one. After the last, the device transitions to keep-alive.
    """

    PACKET_TYPE = PacketType.CLAIM_NUMBER

    device_number: int
    iteration: int

    def encode(self) -> bytes:
        length = _LENGTH[PacketType.CLAIM_NUMBER]
        writer = _write_header(PacketType.CLAIM_NUMBER, self.name, self.device_kind, length)
        writer.u8_at(0x24, self.device_number)
        writer.u8_at(0x25, self.iteration)
        return writer.data()


@dataclass(frozen=True, kw_only=True)
class KeepAlive(DjlPacket):
    """Type ``06`` — the steady-state keep-alive (``research/02`` §2).

    This is the load-bearing packet: it is what makes a virtual CDJ visible,
    and (passively) it is our only source of peers. 0x36 bytes, broadcast every
    ~1.5 s::

        24     D, the device/player number
        25     01 CDJ / 02 mixer
        26-2b  MAC
        2c-2f  IP
        30     peer count, including self
        31-33  000000
        34     flags: 01 CDJ / 02 mixer
        35     trailing: 01 nexus, 64 CDJ-3000, 00 legacy

    Byte ``35`` matters more than its size suggests: the wrong value there can
    make CDJ-3000s set to player 5/6 repeatedly kick themselves off the network
    (``research/02`` §1.6).
    """

    PACKET_TYPE = PacketType.KEEP_ALIVE

    device_number: int
    mac: bytes
    ip: str
    peer_count: int = 1
    #: Byte ``25``. Documented as "01 CDJ / 02 mixer", but real captures show
    #: *both* a CDJ-2000nexus and a DJM-2000nexus alternating between 01 and 02
    #: over the life of one session, so it is not a fixed role byte. Meaning
    #: unresolved; preserved verbatim rather than assumed. FINDINGS.md, C4.
    const_25: int = 0x01
    #: Byte ``34``. This one *is* the role byte: 01 CDJ, 02 mixer, consistent
    #: across every packet observed. ``None`` derives it from the device kind.
    flags: int | None = None
    #: Byte ``35``. research/02 §2 calls ``01`` the "typical CDJ keep-alive"
    #: value, but every real nexus packet captured -- 148 from a CDJ-2000nexus
    #: and 91 from a DJM-2000nexus -- carries ``00``. Since impersonating a
    #: CDJ-2000nexus is the goal, ``00`` is the default. ``64`` remains
    #: required for CDJ-3000 coexistence. FINDINGS.md, C3.
    trailing: int = 0x00

    @property
    def mac_str(self) -> str:
        return format_mac(self.mac)

    def encode(self) -> bytes:
        length = _LENGTH[PacketType.KEEP_ALIVE]
        writer = _write_header(PacketType.KEEP_ALIVE, self.name, self.device_kind, length)
        writer.u8_at(0x24, self.device_number)
        writer.u8_at(0x25, self.const_25)
        writer.put_at(0x26, self.mac)
        writer.put_at(0x2C, _ip_to_bytes(self.ip))
        writer.u8_at(0x30, self.peer_count)
        # 0x31-0x33 stay zero.
        writer.u8_at(
            0x34, self.flags if self.flags is not None else default_role(self.device_kind)
        )
        writer.u8_at(0x35, self.trailing)
        return writer.data()


@dataclass(frozen=True, kw_only=True)
class NumberConflict(DjlPacket):
    """Type ``08`` — "that number is mine" (``research/02`` §1.5).

    Unicast by the *existing owner* to port 50000 of a newcomer that proposed a
    number already in use. A well-behaved peer must both back off on receiving
    one and emit one to defend its own number.

    Caveat from ``research/02`` §1.5: XDJ-XZ and Opus Quad do **not** send
    these, which is why the safe-claim algorithm watches the network first
    rather than relying on conflict packets alone.
    """

    PACKET_TYPE = PacketType.NUMBER_CONFLICT

    device_number: int
    ip: str

    def encode(self) -> bytes:
        length = _LENGTH[PacketType.NUMBER_CONFLICT]
        writer = _write_header(
            PacketType.NUMBER_CONFLICT, self.name, self.device_kind, length
        )
        writer.u8_at(0x24, self.device_number)
        writer.put_at(0x25, _ip_to_bytes(self.ip))
        return writer.data()


@dataclass(frozen=True, kw_only=True)
class UnknownPacket(DjlPacket):
    """A well-formed DJ-Link datagram whose type byte we do not model.

    Returned rather than raised: during capture work an unrecognised packet is
    *data*, and the mixer-side assignment types (``01``/``03``/``05``) plus any
    firmware-specific kinds should show up in the journal instead of vanishing.
    """

    raw_type: int
    payload: bytes

    def encode(self) -> bytes:
        raise NotImplementedError("cannot re-encode an unmodelled packet type")


# -- dispatch --------------------------------------------------------------


def is_djl_packet(data: bytes) -> bool:
    """Cheap guard: does this datagram carry the DJ-Link magic?"""
    return len(data) >= MAGIC_LEN and data[:MAGIC_LEN] == MAGIC


def decode(data: bytes) -> DjlPacket:
    """Decode one UDP-50000 datagram.

    Raises :class:`DecodeError` if the magic is absent or the datagram is too
    short to contain the common header. Unrecognised *types* yield an
    :class:`UnknownPacket` rather than an error.
    """
    if not is_djl_packet(data):
        raise DecodeError(
            f"not a DJ-Link packet: {data[:MAGIC_LEN].hex()!r} != {MAGIC.hex()!r}"
        )
    if len(data) < HEADER_LEN:
        raise DecodeError(f"short DJ-Link packet: {len(data)}B < {HEADER_LEN}B header")

    reader = ByteReader(data)
    packet_type = reader.u8_at(OFF_TYPE)
    common = _common_fields(reader, data)

    def need(minimum: int) -> None:
        if len(data) < minimum:
            raise DecodeError(
                f"type {packet_type:#04x} needs {minimum}B, got {len(data)}B"
            )

    if packet_type == PacketType.KEEP_ALIVE:
        need(0x36)
        return KeepAlive(
            **common,
            device_number=reader.u8_at(0x24),
            const_25=reader.u8_at(0x25),
            mac=reader.raw_at(0x26, 6),
            ip=_ip_from_bytes(reader.raw_at(0x2C, 4)),
            peer_count=reader.u8_at(0x30),
            flags=reader.u8_at(0x34),
            trailing=reader.u8_at(0x35),
        )

    if packet_type == PacketType.HELLO:
        need(0x25)
        return Hello(**common, payload=reader.u8_at(0x24))

    if packet_type == PacketType.CLAIM_MAC:
        need(0x2C)
        return ClaimMac(
            **common,
            iteration=reader.u8_at(0x24),
            flags=reader.u8_at(0x25),
            mac=reader.raw_at(0x26, 6),
        )

    if packet_type == PacketType.CLAIM_IP:
        need(0x32)
        return ClaimIp(
            **common,
            ip=_ip_from_bytes(reader.raw_at(0x24, 4)),
            mac=reader.raw_at(0x28, 6),
            device_number=reader.u8_at(0x2E),
            iteration=reader.u8_at(0x2F),
            role=reader.u8_at(0x30),
            assignment_mode=reader.u8_at(0x31),
        )

    if packet_type == PacketType.CLAIM_NUMBER:
        need(0x26)  # only the two named fields are required
        return ClaimNumber(
            **common,
            device_number=reader.u8_at(0x24),
            iteration=reader.u8_at(0x25),
        )

    if packet_type == PacketType.NUMBER_CONFLICT:
        need(0x29)
        return NumberConflict(
            **common,
            device_number=reader.u8_at(0x24),
            ip=_ip_from_bytes(reader.raw_at(0x25, 4)),
        )

    return UnknownPacket(**common, raw_type=packet_type, payload=data[HEADER_LEN:])
