"""Converting ANLZ tags into the shapes dbserver puts on the wire.

F29 assumed a server could hand a player the analysis bytes rekordbox wrote,
unaltered. It cannot. A real CDJ serving another CDJ **transforms** every one of
them, and the transformations are not cosmetic: the file is big-endian and the
wire is little-endian, and three of the five change the layout as well.

Every rule below is derived by diffing a real load --
``captures/S06-load-and-play``, deck A loading track ``0xc8`` from deck B --
against that track's own ``ANLZ0000.DAT``/``.EXT`` on the medium that was in
deck B. Having both halves is what makes these confirmed rather than guessed;
the tests reproduce the captured bytes exactly.

One field defeats derivation. Both the beat grid and the detail waveform carry
a fifth prefix word (``0x06114a48`` and ``0x0612e0b4`` in that capture) that is
neither in the file nor a function of it, and differs between two replies
recorded seconds apart -- for the same track. See :func:`prefix_opaque`.
"""

from __future__ import annotations

import struct
import time

from . import anlz

__all__ = [
    "vbr_index", "beat_grid", "waveform_preview", "waveform_detail",
    "cue_points", "prefix_opaque",
]

#: Base and rate for the fifth prefix word; see :func:`prefix_opaque`.
_OPAQUE_BASE = 0x06000000
_OPAQUE_RATE = 40_000
_STARTED = time.monotonic()


def prefix_opaque() -> int:
    """The fifth prefix word of ``BEAT_GRID`` and ``WAVEFORM_DETAIL``.

    We cannot derive it. What is known: the two observed values,
    ``0x06114a48`` and ``0x0612e0b4``, are **for the same track in the same
    load**, so it is not a property of the content -- it is per reply. They are
    2.58 s apart and differ by 104,044, i.e. roughly 40,000 per second, which
    makes it a free-running counter or an allocator address on the serving
    deck. Either way a client cannot validate it.

    So this emits a counter of the same shape rather than a constant. That is a
    **hypothesis under test**: after everything else in a load was made
    byte-identical to a real deck's, this word -- sent as zero -- was the only
    remaining difference, and it appears in exactly the two replies that feed
    the main waveform, which is the one thing that still does not display. If a
    non-zero value changes nothing, the cause is outside these replies and this
    should go back to being a constant.
    """
    elapsed = time.monotonic() - _STARTED
    return (_OPAQUE_BASE + int(elapsed * _OPAQUE_RATE)) & 0xFFFFFFFF


#: A tag payload is a run of big-endian words; the wire wants them
#: little-endian. Applied whole-payload where the layout is otherwise unchanged.
def _swap_u32(payload: bytes) -> bytes:
    whole = len(payload) - len(payload) % 4
    swapped = b"".join(
        payload[i + 3 : i + 4] + payload[i + 2 : i + 3]
        + payload[i + 1 : i + 2] + payload[i : i + 1]
        for i in range(0, whole, 4)
    )
    return swapped + payload[whole:]


def vbr_index(dat: anlz.AnlzFile | None) -> bytes:
    """``0x2504`` -> ``0x4502``: the MP3 variable-bitrate seek index.

    The ``PVBR`` payload with every 32-bit word byte-swapped. Nothing else
    changes and the length is a fixed 1604 bytes.

    Probably the request that gates playback: without a table mapping playing
    time to byte offset a player cannot seek within a VBR MP3, so it has no way
    to begin streaming. Erroring on this is where our track loads stopped.

    In the reference capture only the final word is visibly reordered, because
    every other word in that track's index happens to be zero -- and zeros are
    the same in either byte order. Swapping all of them is what the one
    non-palindromic word tells us to do.
    """
    if dat is None:
        return b""
    return _swap_u32(dat.tag_payload(anlz.TAG_VBR_INDEX))


def beat_grid(dat: anlz.AnlzFile | None) -> bytes:
    """``0x2204`` -> ``0x4602``: the beat grid.

    A 20-byte little-endian prefix followed by one 16-byte entry per beat. The
    file stores 8-byte entries -- ``beat_number`` u2, ``tempo`` u2, ``time``
    u4, big-endian -- and the wire keeps the same three fields little-endian,
    then pads each entry to 16 with eight ``0xff`` bytes. Verified against all
    1038 entries of the captured grid.
    """
    if dat is None:
        return b""
    tag = dat.tag(anlz.TAG_BEAT_GRID)
    if tag is None:
        return b""
    payload = tag.payload
    count = len(payload) // 8
    entries = b"".join(
        struct.pack("<HHI", *struct.unpack_from(">HHI", payload, 8 * i))
        + b"\xff" * 8
        for i in range(count)
    )
    # Word 0 is the tag's own constant; word 2 is the entry-block length.
    prefix = struct.pack(
        "<5I", 0x80000, count, len(entries), 1, prefix_opaque()
    )
    return prefix + entries


def waveform_preview(dat: anlz.AnlzFile | None) -> bytes:
    """``0x2004`` -> ``0x4402``: the preview waveform, plus the tiny one.

    The file packs each of the 400 columns into one byte: the low five bits are
    the bar height, the top three a "whiteness" used for shading. The wire
    unpacks that into two bytes per column, height first -- so 800 bytes -- and
    then appends the 100-byte ``PWV2`` tiny waveform verbatim, for 900 in all.

    That trailing 100 bytes is why a plausible-looking "widen each byte"
    implementation still comes out the wrong length.
    """
    if dat is None:
        return b""
    preview = dat.tag_payload(anlz.TAG_WAVEFORM_PREVIEW)
    unpacked = b"".join(bytes((b & 0x1F, b >> 5)) for b in preview)
    return unpacked + dat.tag_payload(anlz.TAG_WAVEFORM_TINY)


def waveform_detail(ext: anlz.AnlzFile | None) -> bytes:
    """``0x2904`` -> ``0x4a02``: the scrolling waveform.

    A 20-byte little-endian prefix and then the ``PWV3`` payload **verbatim** --
    the one analysis blob the wire does not reorder, because its entries are
    single bytes and so have no byte order to get wrong.

    The prefix repeats the entry count twice around the entry width, mirroring
    the three big-endian words in the tag's own extended header.
    """
    if ext is None:
        return b""
    tag = ext.tag(anlz.TAG_WAVEFORM_DETAIL)
    if tag is None:
        return b""
    payload = tag.payload
    # The tag header carries (entry width, entry count, <constant>); the wire
    # wants the count first. 0x96 is the high half of that constant.
    width = 1
    if len(tag.raw) >= 24:
        width = struct.unpack_from(">I", tag.raw, 12)[0] or 1
    prefix = struct.pack(
        "<5I", len(payload), width, len(payload), 0x96, prefix_opaque()
    )
    return prefix + payload


#: Bytes per cue record in the first ``CUE_POINTS`` blob.
CUE_ENTRY_SIZE = 0x24

#: Waveform frames per second. The detail waveform is drawn at 150 columns per
#: second -- the same 150 that appears in its own reply prefix -- and a cue's
#: position travels as a *frame index* rather than a time, so the player can
#: place the marker on the waveform without doing the arithmetic itself.
WAVEFORM_FPS = 150


def _frame_of(milliseconds: int) -> int:
    """A cue time in waveform frames, truncated as the hardware truncates.

    271 ms becomes 40, not 41 -- confirmed against all three cues in the
    reference capture, which is what rules out rounding.
    """
    return milliseconds * WAVEFORM_FPS // 1000


def cue_points(dat: anlz.AnlzFile | None) -> tuple[bytes, int, bytes]:
    """``0x2104`` -> ``0x4702``: memory points and hot cues.

    Returns ``(records, count, times)`` for the reply's two blobs: ``count``
    records of :data:`CUE_ENTRY_SIZE` bytes, then one little-endian
    ``(time, loop_time)`` pair per cue.

    A record is ``[u2 order][u2 hot cue][u4 0][u4 0][u4 frame]`` and twenty
    zero bytes. Cues go out **sorted by time**, not in the order the file
    stores them -- rekordbox had written this track's three cues newest-first.
    """
    if dat is None:
        return b"", 0, b""
    tag = dat.tag(anlz.TAG_CUES)
    if tag is None:
        return b"", 0, b""

    cues = sorted(_iter_cues(tag.payload), key=lambda cue: cue[2])
    records = b"".join(
        struct.pack("<HHIII", order, hot_cue, 0, 0, _frame_of(time))
        + bytes(CUE_ENTRY_SIZE - 16)
        for order, hot_cue, time, _loop in cues
    )
    times = b"".join(
        struct.pack("<II", time, loop) for _o, _h, time, loop in cues
    )
    return records, len(cues), times


def _iter_cues(payload: bytes):
    """Walk the ``PCPT`` sub-entries of a ``PCOB`` payload.

    Yields ``(order, hot_cue, time, loop_time)``. A malformed or truncated
    entry ends the walk rather than raising: a bad cue should cost the cue,
    not the load.
    """
    offset = 0
    while offset + 40 <= len(payload):
        if payload[offset : offset + 4] != b"PCPT":
            break
        entry_len = struct.unpack_from(">I", payload, offset + 8)[0]
        if entry_len < 40 or offset + entry_len > len(payload):
            break
        hot_cue = struct.unpack_from(">I", payload, offset + 12)[0]
        order = struct.unpack_from(">H", payload, offset + 28)[0]
        time, loop = struct.unpack_from(">II", payload, offset + 32)
        yield order, hot_cue, time, loop
        offset += entry_len
