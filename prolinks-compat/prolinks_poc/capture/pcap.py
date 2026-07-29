"""Read pcap / pcapng captures and the annotated-hex dumps libcdj ships.

Lets every codec be exercised against **real Pioneer traffic** with no hardware
attached, which is the difference between "our encoder agrees with our decoder"
and "our decoder agrees with a CDJ". Round-trip tests cannot catch a shared
misreading of the specification; a capture can.

Written against the format specifications (pcapng draft; the classic libpcap
header layout), not derived from any reference implementation. It is a
deliberately small subset: Ethernet over IPv4, UDP and TCP, which is all a
DJ-Link capture contains.

**Licensing.** The captures under ``research/ref-repos/`` are recordings of
Pioneer hardware behaviour -- protocol facts, not authored code. They are read
in place and never copied into this repository, so the EPL/unlicensed status of
the surrounding projects does not attach to anything we ship. Tests that use
them skip when ``ref-repos`` is absent. Our own captures from real hardware
become the committed fixtures.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator

from ..proto.errors import DecodeError

__all__ = ["Packet", "read_capture", "read_pcapng", "read_pcap", "read_dump_file"]

# pcapng block types
_SHB = 0x0A0D0D0A  # section header
_IDB = 0x00000001  # interface description
_SPB = 0x00000003  # simple packet
_EPB = 0x00000006  # enhanced packet

# classic pcap magics, in both endiannesses and both time resolutions
_PCAP_MAGICS = {
    0xA1B2C3D4: (">", 1_000_000),
    0xD4C3B2A1: ("<", 1_000_000),
    0xA1B23C4D: (">", 1_000_000_000),
    0x4D3CB2A1: ("<", 1_000_000_000),
}

LINKTYPE_ETHERNET = 1
LINKTYPE_RAW = 101
LINKTYPE_LOOPBACK = 0


@dataclass(frozen=True)
class Packet:
    """One transport-layer payload lifted out of a capture."""

    index: int
    timestamp: float
    protocol: str  # "udp" | "tcp"
    src_ip: str
    src_port: int
    dst_ip: str
    dst_port: int
    payload: bytes
    #: TCP only: the sequence number, needed to reassemble a dbserver stream.
    tcp_seq: int | None = None

    @property
    def is_broadcast(self) -> bool:
        return self.dst_ip.endswith(".255")

    def __str__(self) -> str:
        return (
            f"#{self.index:<5} {self.timestamp:>12.6f}  {self.protocol}  "
            f"{self.src_ip}:{self.src_port} -> {self.dst_ip}:{self.dst_port}  "
            f"{len(self.payload)}B"
        )


def read_capture(path: Path | str) -> Iterator[Packet]:
    """Dispatch on the file's magic. Handles pcapng, pcap and libcdj dumps."""
    path = Path(path)
    head = path.open("rb").read(4)
    if len(head) < 4:
        raise DecodeError(f"{path} is too short to be a capture")

    if struct.unpack(">I", head)[0] == _SHB:
        yield from read_pcapng(path)
        return
    for endian in (">", "<"):
        if struct.unpack(f"{endian}I", head)[0] in _PCAP_MAGICS:
            yield from read_pcap(path)
            return
    raise DecodeError(
        f"{path}: not a pcap or pcapng (magic {head.hex()}). "
        "For libcdj annotated-hex dumps use read_dump_file()."
    )


# -- pcapng ----------------------------------------------------------------


def read_pcapng(path: Path | str) -> Iterator[Packet]:
    """Iterate a pcapng file.

    Only the blocks that carry packets are interpreted; options, name
    resolution and statistics blocks are skipped by length. Byte order is
    per-section, taken from the section header's byte-order magic.
    """
    data = Path(path).read_bytes()
    offset = 0
    endian = "<"
    #: Per-interface link type and timestamp resolution, indexed in order of
    #: appearance -- which is how enhanced packet blocks refer to them.
    interfaces: list[tuple[int, int]] = []
    index = 0

    while offset + 12 <= len(data):
        block_type = struct.unpack_from(f"{endian}I", data, offset)[0]

        if struct.unpack_from(">I", data, offset)[0] == _SHB:
            # A new section: re-derive the byte order before reading anything.
            byte_order_magic = struct.unpack_from(">I", data, offset + 8)[0]
            endian = ">" if byte_order_magic == 0x1A2B3C4D else "<"
            interfaces = []
            block_type = _SHB

        block_length = struct.unpack_from(f"{endian}I", data, offset + 4)[0]
        if block_length < 12 or offset + block_length > len(data):
            break
        body = data[offset + 8 : offset + block_length - 4]

        if block_type == _IDB:
            link_type = struct.unpack_from(f"{endian}H", body, 0)[0]
            interfaces.append((link_type, _timestamp_divisor(body, endian)))

        elif block_type == _EPB:
            interface_id, ts_high, ts_low, captured = struct.unpack_from(
                f"{endian}IIII", body, 0
            )
            link_type, divisor = (
                interfaces[interface_id]
                if interface_id < len(interfaces)
                else (LINKTYPE_ETHERNET, 1_000_000)
            )
            timestamp = ((ts_high << 32) | ts_low) / divisor
            frame = body[20 : 20 + captured]
            index += 1
            packet = _parse_frame(frame, link_type, index, timestamp)
            if packet is not None:
                yield packet

        elif block_type == _SPB:
            link_type = interfaces[0][0] if interfaces else LINKTYPE_ETHERNET
            index += 1
            packet = _parse_frame(body[4:], link_type, index, 0.0)
            if packet is not None:
                yield packet

        offset += block_length


def _timestamp_divisor(idb_body: bytes, endian: str) -> int:
    """Read the ``if_tsresol`` option (code 9), defaulting to microseconds."""
    offset = 8  # past link_type(2), reserved(2), snaplen(4)
    while offset + 4 <= len(idb_body):
        code, length = struct.unpack_from(f"{endian}HH", idb_body, offset)
        if code == 0:  # opt_endofopt
            break
        if code == 9 and length >= 1:
            resolution = idb_body[offset + 4]
            # High bit set means a power of two; otherwise a power of ten.
            return (1 << (resolution & 0x7F)) if resolution & 0x80 else 10 ** resolution
        offset += 4 + ((length + 3) & ~3)
    return 1_000_000


# -- classic pcap ----------------------------------------------------------


def read_pcap(path: Path | str) -> Iterator[Packet]:
    data = Path(path).read_bytes()
    magic = struct.unpack_from(">I", data, 0)[0]
    if magic not in _PCAP_MAGICS:
        magic = struct.unpack_from("<I", data, 0)[0]
    if magic not in _PCAP_MAGICS:
        raise DecodeError(f"{path}: not a classic pcap")
    endian, divisor = _PCAP_MAGICS[magic]

    link_type = struct.unpack_from(f"{endian}I", data, 20)[0]
    offset = 24
    index = 0
    while offset + 16 <= len(data):
        seconds, fraction, captured, _original = struct.unpack_from(
            f"{endian}IIII", data, offset
        )
        offset += 16
        frame = data[offset : offset + captured]
        offset += captured
        index += 1
        packet = _parse_frame(frame, link_type, index, seconds + fraction / divisor)
        if packet is not None:
            yield packet


# -- frame dissection ------------------------------------------------------


def _parse_frame(
    frame: bytes, link_type: int, index: int, timestamp: float
) -> Packet | None:
    """Ethernet/raw -> IPv4 -> UDP|TCP. Returns ``None`` for anything else."""
    if link_type == LINKTYPE_ETHERNET:
        if len(frame) < 14:
            return None
        ethertype = struct.unpack_from(">H", frame, 12)[0]
        offset = 14
        if ethertype == 0x8100:  # 802.1Q VLAN tag
            if len(frame) < 18:
                return None
            ethertype = struct.unpack_from(">H", frame, 16)[0]
            offset = 18
        if ethertype != 0x0800:  # not IPv4
            return None
    elif link_type == LINKTYPE_RAW:
        offset = 0
    elif link_type == LINKTYPE_LOOPBACK:
        offset = 4
    else:
        return None

    return _parse_ipv4(frame, offset, index, timestamp)


def _parse_ipv4(frame: bytes, offset: int, index: int, timestamp: float) -> Packet | None:
    if len(frame) < offset + 20:
        return None
    version_ihl = frame[offset]
    if version_ihl >> 4 != 4:
        return None
    header_length = (version_ihl & 0x0F) * 4
    protocol = frame[offset + 9]
    total_length = struct.unpack_from(">H", frame, offset + 2)[0]
    src_ip = ".".join(str(b) for b in frame[offset + 12 : offset + 16])
    dst_ip = ".".join(str(b) for b in frame[offset + 16 : offset + 20])

    # Trust the IP total-length field over the captured frame length, so
    # Ethernet padding on short frames is not mistaken for payload.
    end = min(len(frame), offset + total_length) if total_length else len(frame)
    transport = offset + header_length

    if protocol == 17:  # UDP
        if transport + 8 > len(frame):
            return None
        src_port, dst_port, udp_length = struct.unpack_from(">HHH", frame, transport)
        payload_end = min(end, transport + max(udp_length, 8))
        return Packet(
            index=index, timestamp=timestamp, protocol="udp",
            src_ip=src_ip, src_port=src_port, dst_ip=dst_ip, dst_port=dst_port,
            payload=frame[transport + 8 : payload_end],
        )

    if protocol == 6:  # TCP
        if transport + 20 > len(frame):
            return None
        src_port, dst_port, sequence = struct.unpack_from(">HHI", frame, transport)
        data_offset = (frame[transport + 12] >> 4) * 4
        payload = frame[transport + data_offset : end]
        return Packet(
            index=index, timestamp=timestamp, protocol="tcp",
            src_ip=src_ip, src_port=src_port, dst_ip=dst_ip, dst_port=dst_port,
            payload=payload, tcp_seq=sequence,
        )

    return None


# -- libcdj annotated-hex dumps -------------------------------------------


def read_dump_file(path: Path | str) -> bytes:
    """Parse an annotated-hex packet dump into raw bytes.

    Format is ``#``-prefixed comment lines followed by whitespace-separated
    ``0x..`` byte literals::

        # 'XDJ-1000' type=06:CDJ_DEVICE_KEEP_ALIVE
        0x51 0x73 0x70 0x74 ...

    Useful as ready-made golden vectors for the DJ-Link codec. Treat the field
    *values* with some suspicion -- the MAC in the keep-alive dump is
    ``12:13:14:15:16:17``, which is plainly synthetic -- but the *structure*
    is authoritative.
    """
    out = bytearray()
    for line in Path(path).read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        for token in line.split():
            out.append(int(token, 16))
    return bytes(out)
