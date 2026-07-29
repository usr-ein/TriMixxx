"""PioString — the variable-length string used throughout ``export.pdb``.

Three forms, discriminated by the first byte (``research/05`` §4):

``0x40``
    Long ASCII. A 2-byte little-endian length follows, holding ``actual + 4``,
    then one padding byte, then the text.
``0x90``
    UTF-16. Same ``actual + 4`` length field, then the text as **UTF-16
    big-endian** -- note big-endian here, where the NFS layer's Pioneer strings
    are little-endian. The two are unrelated conventions in unrelated formats,
    and mixing them up produces plausible-looking mojibake rather than an
    error.
default
    Short ASCII. The byte itself packs the length: ``(byte - 1) // 2 - 1``
    characters follow inline. The low bit is a flag, so the value runs
    ``2 * length + 3``.

Encoding is implemented as well as decoding, because generating a ``.pdb`` is
what objective 2 eventually needs, and a round-trip test is the cheapest way to
be sure the length arithmetic is right in both directions.
"""

from __future__ import annotations

import struct

from .errors import DecodeError

__all__ = ["read_piostring", "encode_piostring", "SELECTOR_LONG_ASCII", "SELECTOR_UTF16"]

SELECTOR_LONG_ASCII = 0x40
SELECTOR_UTF16 = 0x90

#: A short-ASCII length byte cannot exceed this, since the encoded value is
#: ``2 * length + 3`` and the field is one byte.
MAX_SHORT_LENGTH = (0xFF - 1) // 2 - 1


def read_piostring(data: bytes, offset: int) -> str:
    """Decode the PioString at *offset*.

    Decoding is lenient about the text but strict about the framing: a length
    that runs past the end of the buffer raises rather than truncating, while
    undecodable characters become replacements. A corrupt title should cost
    that title, not the whole database.
    """
    if offset < 0 or offset >= len(data):
        raise DecodeError(f"PioString offset {offset} outside {len(data)}-byte buffer")

    selector = data[offset]

    if selector == SELECTOR_LONG_ASCII:
        length = _framed_length(data, offset, "long ASCII")
        start = offset + 4  # selector(1) + length(2) + padding(1)
        _require(data, start, length, "long ASCII")
        return data[start : start + length].decode("ascii", errors="replace")

    if selector == SELECTOR_UTF16:
        length = _framed_length(data, offset, "UTF-16")
        start = offset + 3  # selector(1) + length(2)
        _require(data, start, length, "UTF-16")
        return data[start : start + length].decode("utf-16-be", errors="replace")

    length = (selector - 1) // 2 - 1
    if length < 0:
        # A selector of 0 or 1 encodes the empty string; anything else this
        # small is a malformed row, and returning "" keeps one bad row from
        # aborting the table walk.
        return ""
    start = offset + 1
    _require(data, start, length, "short ASCII")
    return data[start : start + length].decode("ascii", errors="replace")


def _framed_length(data: bytes, offset: int, kind: str) -> int:
    if offset + 3 > len(data):
        raise DecodeError(f"truncated {kind} PioString header at {offset}")
    (stored,) = struct.unpack_from("<H", data, offset + 1)
    length = stored - 4
    if length < 0:
        raise DecodeError(f"{kind} PioString at {offset} has stored length {stored} < 4")
    return length


def _require(data: bytes, start: int, length: int, kind: str) -> None:
    if start + length > len(data):
        raise DecodeError(
            f"{kind} PioString at {start} claims {length} bytes, "
            f"only {len(data) - start} remain"
        )


def encode_piostring(text: str) -> bytes:
    """Encode *text*, choosing the narrowest form that fits.

    ASCII that fits the short form uses it, since that is what rekordbox
    itself emits for the overwhelming majority of strings; anything non-ASCII
    goes to UTF-16BE.
    """
    try:
        ascii_bytes = text.encode("ascii")
    except UnicodeEncodeError:
        encoded = text.encode("utf-16-be")
        return bytes([SELECTOR_UTF16]) + struct.pack("<H", len(encoded) + 4) + encoded

    if len(ascii_bytes) <= MAX_SHORT_LENGTH:
        return bytes([2 * (len(ascii_bytes) + 1) + 1]) + ascii_bytes
    return (
        bytes([SELECTOR_LONG_ASCII])
        + struct.pack("<H", len(ascii_bytes) + 4)
        + b"\x00"
        + ascii_bytes
    )
