"""Golden tests for the ANLZ-to-dbserver conversions.

Every expected value here comes from ``captures/S06-load-and-play`` -- a real
CDJ-2000NXS answering another one's load of track ``0xc8`` -- paired with that
same track's ``ANLZ0000.DAT``/``.EXT`` from the USB stick that was in the
serving deck. Having the input and the output of a real implementation is what
makes these confirmations rather than guesses, and it is the only reason we
know the wire format differs from the file format at all.

The tag bytes below are verbatim from that medium.
"""

from __future__ import annotations

import struct

import pytest

from prolinks_poc.proto import analysis_wire as wire
from prolinks_poc.proto.anlz import AnlzFile


def container(*tags: bytes) -> AnlzFile:
    """Wrap real tag bytes in a minimal ``PMAI`` header."""
    body = b"".join(tags)
    header = b"PMAI" + struct.pack(">II", 28, 28 + len(body)) + bytes(16)
    return AnlzFile(header + body)


def tag(fourcc: bytes, header_size: int, payload: bytes, extra: bytes = b"") -> bytes:
    total = 12 + len(extra) + len(payload)
    return fourcc + struct.pack(">II", header_size, total) + extra + payload


# Three real cues, newest-first as rekordbox wrote them.
PCOB = bytes.fromhex(
    "50434f4200000018000000c00000000100000003ffffffff504350540000001c"
    "00000038000000030000000000010000ffffffff010003e800015d0bffffffff"
    "00000000000000000000000000000000504350540000001c0000003800000002"
    "0000000000010000ffffffff010003e80000f99effffffff0000000000000000"
    "0000000000000000504350540000001c00000038000000010000000000010000"
    "ffffffff010003e80000010fffffffff00000000000000000000000000000000"
)

# The first three beats of that track's real grid: beat number, tempo x100,
# and time in ms, all big-endian.
PQTZ_BEATS_3 = bytes.fromhex(
    "000133910000010f00023391000002d5000333910000049c"
)
#: The tag's own extended header: unknown, the 0x80000 constant, beat count.
PQTZ_EXTRA = bytes.fromhex("00000000000800000000040e")


def test_cue_points_match_a_real_reply_byte_for_byte():
    """The full ``CUE_POINTS`` payload, both blobs, exactly as a deck sent it."""
    records, count, times = wire.cue_points(container(PCOB))

    assert count == 3
    # Sorted by time, though the file stores them 3, 2, 1.
    assert times == bytes.fromhex(
        "0f010000ffffffff9ef90000ffffffff0b5d0100ffffffff"
    )
    assert records == bytes.fromhex(
        "000101000000000000000000280000000000000000000000000000000000000000000000"
        "000102000000000000000000712500000000000000000000000000000000000000000000"
        "000103000000000000000000" "5b340000" "0000000000000000000000000000000000000000"
    )
    assert len(records) == count * wire.CUE_ENTRY_SIZE


def test_cue_frames_truncate_rather_than_round():
    """271 ms is frame 40, not 41 -- as the hardware writes it."""
    assert wire._frame_of(271) == 40
    assert wire._frame_of(63902) == 9585
    assert wire._frame_of(89355) == 13403


def test_beat_grid_prefix_and_entries():
    grid = wire.beat_grid(
        container(tag(b"PQTZ", 24, PQTZ_BEATS_3, extra=PQTZ_EXTRA))
    )
    constant, count, entry_bytes, one, opaque = struct.unpack_from("<5I", grid, 0)
    assert (constant, count, entry_bytes, one) == (0x80000, 3, 48, 1)
    assert opaque == wire.PREFIX_OPAQUE

    # The file's 8-byte big-endian entry becomes 8 little-endian bytes plus a
    # 0xff pad, for 16 -- confirmed against all 1038 entries of the capture.
    assert grid[20:36] == bytes.fromhex("010091330f010000") + b"\xff" * 8
    assert grid[36:52] == bytes.fromhex("02009133d5020000") + b"\xff" * 8
    assert len(grid) == 20 + 3 * 16


def test_vbr_index_byte_swaps_every_word():
    """The whole point: only one word in the real index is non-palindromic, and
    it is reordered. Zeros hid the rule everywhere else."""
    payload = bytes(1596) + bytes.fromhex("0000000001597680")
    out = wire.vbr_index(container(tag(b"PVBR", 16, payload, extra=bytes(4))))
    assert len(out) == 1604
    assert out[-8:] == bytes.fromhex("0000000080765901")


def test_waveform_preview_unpacks_height_and_whiteness():
    """Each packed byte becomes ``(height, whiteness)``; the tiny waveform is
    appended verbatim, which is what makes the reply 900 rather than 800."""
    preview = bytes.fromhex("110ca2a28282a3a3")
    tiny = bytes(range(100))
    out = wire.waveform_preview(
        container(tag(b"PWAV", 20, preview, extra=bytes(8)),
                  tag(b"PWV2", 20, tiny, extra=bytes(8)))
    )
    assert out[:16] == bytes.fromhex("11000c00020502050204020403050305")
    assert out[len(preview) * 2:] == tiny
    assert len(out) == len(preview) * 2 + 100


def test_waveform_detail_is_the_payload_verbatim_behind_a_prefix():
    payload = bytes(range(256)) * 3
    out = wire.waveform_detail(
        container(tag(b"PWV3", 24, payload, extra=struct.pack(">III", 1, len(payload), 0x960000)))
    )
    count, width, count2, fps, opaque = struct.unpack_from("<5I", out, 0)
    assert (count, width, count2, fps) == (len(payload), 1, len(payload), 0x96)
    assert opaque == wire.PREFIX_OPAQUE
    assert out[20:] == payload


@pytest.mark.parametrize(
    "convert", [wire.vbr_index, wire.beat_grid, wire.waveform_preview,
                wire.waveform_detail],
)
def test_a_missing_file_or_tag_yields_an_empty_blob(convert):
    """A track analysed by an older rekordbox lacks the newer tags, and the
    protocol has a representation for an empty blob. A missing waveform must
    cost the waveform, not the load."""
    assert convert(None) == b""
    assert convert(container()) == b""


def test_missing_cue_tag_yields_an_empty_reply():
    assert wire.cue_points(None) == (b"", 0, b"")
    assert wire.cue_points(container()) == (b"", 0, b"")
