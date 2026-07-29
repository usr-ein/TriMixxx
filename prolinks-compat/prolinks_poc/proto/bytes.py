"""Byte-level reader/writer primitives — **the portability seam**.

Every codec in :mod:`prolinks_poc.proto` is written against these two classes,
so porting to Qt means reimplementing exactly this file (as
``src/network/prolink/rpc/xdrbuffer.{h,cpp}`` plus a small packet helper) and
transcribing the callers mechanically.

Both DJ-Link and XDR are **big-endian**, so big-endian is the default and
little-endian accessors carry an explicit ``_le`` suffix. The only
little-endian numbers in the whole protocol family are inside ANLZ cue/grid
records and the Pioneer UTF-16LE strings.

Mapping to the C++ port:

===========================  ==================================================
Python                       C++/Qt
===========================  ==================================================
``ByteWriter``               ``XdrWriter`` over ``QByteArray``
``ByteReader``               ``XdrReader``; exceptions become a sticky ``m_ok``
``reader.u32()``             ``QDataStream(QDataStream::BigEndian) >> quint32``
``writer.data()``            ``const QByteArray&``
===========================  ==================================================
"""

from __future__ import annotations

import struct

from .errors import DecodeError

__all__ = ["ByteReader", "ByteWriter", "align4", "hexdump"]


def align4(n: int) -> int:
    """Round *n* up to the next multiple of 4 (XDR alignment)."""
    return (n + 3) & ~3


class ByteWriter:
    """Append-only big-endian byte builder.

    Also supports fixed-size, offset-addressed construction via
    :meth:`allocate` + :meth:`put_at`, which is how the DJ-Link packets are
    built: their specs in ``research/02`` are offset tables, so building them
    at named offsets keeps the code checkable against the document line by
    line. In C++ that is ``QByteArray(len, 0)`` plus ``memcpy``.
    """

    __slots__ = ("_buf",)

    def __init__(self, initial: bytes | bytearray = b"") -> None:
        self._buf = bytearray(initial)

    # -- sequential ------------------------------------------------------

    def u8(self, value: int) -> "ByteWriter":
        self._buf.append(value & 0xFF)
        return self

    def u16(self, value: int) -> "ByteWriter":
        self._buf += struct.pack(">H", value & 0xFFFF)
        return self

    def u32(self, value: int) -> "ByteWriter":
        self._buf += struct.pack(">I", value & 0xFFFFFFFF)
        return self

    def i32(self, value: int) -> "ByteWriter":
        self._buf += struct.pack(">i", value)
        return self

    def u32_le(self, value: int) -> "ByteWriter":
        self._buf += struct.pack("<I", value & 0xFFFFFFFF)
        return self

    def raw(self, data: bytes) -> "ByteWriter":
        self._buf += data
        return self

    def zeros(self, count: int) -> "ByteWriter":
        self._buf += bytes(count)
        return self

    def pad4(self) -> "ByteWriter":
        """Pad to the next 4-byte boundary (XDR)."""
        self._buf += bytes(align4(len(self._buf)) - len(self._buf))
        return self

    def ascii_fixed(self, text: str, width: int) -> "ByteWriter":
        """Write *text* as ASCII in a fixed *width* field, NUL-padded.

        Over-long text is truncated. This is the DJ-Link 20-byte device-name
        field (``research/02`` §4.1).
        """
        encoded = text.encode("ascii", errors="replace")[:width]
        self._buf += encoded + bytes(width - len(encoded))
        return self

    # -- fixed-size, offset addressed ------------------------------------

    def allocate(self, size: int) -> "ByteWriter":
        """Grow the buffer to *size* zero bytes, for :meth:`put_at` use."""
        if size > len(self._buf):
            self._buf += bytes(size - len(self._buf))
        return self

    def put_at(self, offset: int, data: bytes) -> "ByteWriter":
        end = offset + len(data)
        if end > len(self._buf):
            raise ValueError(
                f"put_at({offset:#04x}, {len(data)}B) exceeds buffer of {len(self._buf)}B"
            )
        self._buf[offset:end] = data
        return self

    def u8_at(self, offset: int, value: int) -> "ByteWriter":
        return self.put_at(offset, bytes([value & 0xFF]))

    def u32_at(self, offset: int, value: int) -> "ByteWriter":
        return self.put_at(offset, struct.pack(">I", value & 0xFFFFFFFF))

    # -- output ----------------------------------------------------------

    def data(self) -> bytes:
        return bytes(self._buf)

    def __len__(self) -> int:
        return len(self._buf)

    def __repr__(self) -> str:  # pragma: no cover - debugging aid
        return f"<ByteWriter {len(self._buf)}B {self._buf.hex()}>"


class ByteReader:
    """Bounds-checked big-endian reader over an immutable buffer.

    Every accessor validates against the remaining length and raises
    :class:`~prolinks_poc.proto.errors.DecodeError` rather than truncating or
    over-reading. Length-prefixed reads validate the prefix *before*
    allocating, so a hostile or corrupt datagram claiming a 4 GiB payload costs
    nothing.
    """

    __slots__ = ("_buf", "_pos")

    def __init__(self, data: bytes, pos: int = 0) -> None:
        self._buf = bytes(data)
        self._pos = pos

    # -- position --------------------------------------------------------

    @property
    def pos(self) -> int:
        return self._pos

    def remaining(self) -> int:
        return len(self._buf) - self._pos

    def at_end(self) -> bool:
        return self._pos >= len(self._buf)

    def seek(self, pos: int) -> None:
        if pos < 0 or pos > len(self._buf):
            raise DecodeError(f"seek to {pos} outside 0..{len(self._buf)}")
        self._pos = pos

    def skip(self, count: int) -> None:
        self._require(count)
        self._pos += count

    def _require(self, count: int) -> None:
        if count < 0:
            raise DecodeError(f"negative read length {count}")
        if self._pos + count > len(self._buf):
            raise DecodeError(
                f"truncated: need {count}B at offset {self._pos}, "
                f"only {self.remaining()}B remain"
            )

    # -- scalars ---------------------------------------------------------

    def u8(self) -> int:
        self._require(1)
        value = self._buf[self._pos]
        self._pos += 1
        return value

    def u16(self) -> int:
        self._require(2)
        (value,) = struct.unpack_from(">H", self._buf, self._pos)
        self._pos += 2
        return value

    def u32(self) -> int:
        self._require(4)
        (value,) = struct.unpack_from(">I", self._buf, self._pos)
        self._pos += 4
        return value

    def i32(self) -> int:
        self._require(4)
        (value,) = struct.unpack_from(">i", self._buf, self._pos)
        self._pos += 4
        return value

    def u32_le(self) -> int:
        self._require(4)
        (value,) = struct.unpack_from("<I", self._buf, self._pos)
        self._pos += 4
        return value

    # -- blocks ----------------------------------------------------------

    def raw(self, count: int) -> bytes:
        self._require(count)
        value = self._buf[self._pos : self._pos + count]
        self._pos += count
        return value

    def peek(self, count: int) -> bytes:
        """Read *count* bytes without advancing."""
        self._require(count)
        return self._buf[self._pos : self._pos + count]

    def raw_at(self, offset: int, count: int) -> bytes:
        """Absolute read that does not disturb the cursor.

        Used by the DJ-Link decoders, whose specs are offset tables.
        """
        if offset < 0 or offset + count > len(self._buf):
            raise DecodeError(
                f"raw_at({offset:#04x}, {count}) outside buffer of {len(self._buf)}B"
            )
        return self._buf[offset : offset + count]

    def u8_at(self, offset: int) -> int:
        return self.raw_at(offset, 1)[0]

    def ascii_fixed(self, width: int) -> str:
        """Read a fixed-width NUL-padded ASCII field.

        Decoded leniently: real hardware has been observed putting non-ASCII
        bytes in name fields, and dropping a whole packet over a stray byte in
        a cosmetic field would be worse than showing a replacement character.
        Callers that need the literal bytes (M1's name-casing capture) keep
        them separately.
        """
        return self.raw(width).split(b"\x00", 1)[0].decode("ascii", errors="replace")

    def all_remaining(self) -> bytes:
        value = self._buf[self._pos :]
        self._pos = len(self._buf)
        return value

    def __repr__(self) -> str:  # pragma: no cover - debugging aid
        return f"<ByteReader pos={self._pos}/{len(self._buf)}>"


def hexdump(data: bytes, width: int = 16, indent: str = "") -> str:
    """Classic offset / hex / ASCII dump, for ``prolinks sniff --hex``."""
    lines = []
    for offset in range(0, len(data), width):
        chunk = data[offset : offset + width]
        hex_part = " ".join(f"{b:02x}" for b in chunk)
        text = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in chunk)
        lines.append(f"{indent}{offset:04x}  {hex_part:<{width * 3 - 1}}  |{text}|")
    return "\n".join(lines)
