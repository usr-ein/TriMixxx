"""XDR tests, with emphasis on Pioneer's UTF-16LE deviation and on bounds.

Two things here are worth more than the rest put together:

* the UTF-16LE string byte layout, because getting the prefix wrong (character
  count instead of byte count) produces ``NFSERR_NOENT`` and no other clue;
* rejecting an implausible length prefix **without allocating**, because that
  property has to survive into the C++ port where the consequence of losing it
  is a remote memory-exhaustion bug rather than a Python exception.
"""

from __future__ import annotations

import pytest

from prolinks_poc.proto.bytes import align4
from prolinks_poc.proto.errors import DecodeError
from prolinks_poc.proto.xdr import XdrReader, XdrWriter


def test_integers_are_big_endian():
    assert XdrWriter().u32(1).data() == b"\x00\x00\x00\x01"
    assert XdrWriter().u32(0xDEADBEEF).data() == bytes.fromhex("deadbeef")
    assert XdrReader(bytes.fromhex("deadbeef")).u32() == 0xDEADBEEF


def test_utf16le_string_uses_a_byte_length_prefix():
    """``/C/`` is 3 characters but 6 UTF-16LE bytes. The prefix must say 6."""
    encoded = XdrWriter().string_utf16le("/C/").data()
    assert encoded[:4] == b"\x00\x00\x00\x06", "prefix must be the BYTE count"
    assert encoded[4:10] == b"/\x00C\x00/\x00"
    assert len(encoded) == 4 + align4(6)  # 6 bytes padded to 8
    assert encoded == bytes.fromhex("000000062f0043002f000000")


@pytest.mark.parametrize("path", ["/B/", "/C/", "/", "PIONEER", "export.pdb"])
def test_utf16le_round_trip(path):
    encoded = XdrWriter().string_utf16le(path).data()
    assert XdrReader(encoded).string_utf16le() == path


def test_string_utf16le_raw_returns_the_literal_bytes():
    """Experiment E3 needs what the hardware said, not our reading of it."""
    encoded = XdrWriter().string_utf16le("/C/").data()
    decoded, raw = XdrReader(encoded).string_utf16le_raw()
    assert decoded == "/C/"
    assert raw == b"/\x00C\x00/\x00"


def test_opaque_fixed_is_padded_but_unprefixed():
    """A 32-byte filehandle is already aligned, so it travels bare."""
    handle = bytes(range(32))
    encoded = XdrWriter().opaque_fixed(handle).data()
    assert encoded == handle
    assert XdrReader(encoded).opaque_fixed(32) == handle


def test_opaque_fixed_pads_an_unaligned_length():
    encoded = XdrWriter().opaque_fixed(b"abc").data()
    assert encoded == b"abc\x00"


def test_opaque_var_pads_to_four():
    encoded = XdrWriter().opaque_var(b"hello").data()
    assert encoded == b"\x00\x00\x00\x05hello\x00\x00\x00"
    assert XdrReader(encoded).opaque_var() == b"hello"


def test_absurd_length_is_rejected_without_allocating():
    """The property that must survive into C++.

    A reply claiming a 4 GiB payload should cost nothing at all. If this test
    ever starts passing by actually allocating, the port has a remote DoS.
    """
    hostile = b"\xff\xff\xff\xff" + b"tiny"
    with pytest.raises(DecodeError, match="refusing to allocate"):
        XdrReader(hostile).opaque_var()


def test_length_beyond_the_datagram_is_rejected():
    truncated = b"\x00\x00\x01\x00" + b"only eight"
    with pytest.raises(DecodeError, match="only .* remain"):
        XdrReader(truncated).opaque_var()


def test_reader_rejects_reading_past_the_end():
    reader = XdrReader(b"\x00\x00\x00\x01")
    assert reader.u32() == 1
    with pytest.raises(DecodeError, match="truncated"):
        reader.u32()


def test_array_count_is_capped():
    with pytest.raises(DecodeError, match="exceeds cap"):
        XdrReader(b"\xff\xff\xff\xff").array_u32()


def test_boolean_round_trip():
    assert XdrReader(XdrWriter().boolean(True).data()).boolean() is True
    assert XdrReader(XdrWriter().boolean(False).data()).boolean() is False


def test_align4():
    assert [align4(n) for n in (0, 1, 3, 4, 5, 8)] == [0, 4, 4, 4, 8, 8]
