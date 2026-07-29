"""ANLZ analysis files — ``ANLZ####.DAT`` / ``.EXT``.

Per-track analysis written by rekordbox alongside the audio: beat grid, cue
points, and waveform data. A player fetches these from the medium's owner over
dbserver when it loads a track, and appears to **refuse the load** if they are
unavailable -- browsing works fine without them, but pressing load does not.

The container is trivially simple, which is fortunate: a ``PMAI`` header
followed by a flat sequence of tags, each a 4-character identifier, a header
size, a total size, and a payload. Everything is **big-endian**, unlike the
little-endian pdb that references these files.

This extracts tag payloads without interpreting them. That is deliberate: to
*serve* analysis data we only need to hand a player the same bytes rekordbox
wrote, and parsing beat grids into structures we would only re-serialise would
add a decoding step -- and a chance to get it wrong -- for no gain. A consuming
implementation (the Mixxx side) needs the interpretation; a serving one does
not.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

from .errors import DecodeError

__all__ = ["AnlzTag", "AnlzFile", "TAG_BEAT_GRID", "TAG_WAVEFORM_PREVIEW",
           "TAG_CUES", "TAG_CUES_EXT", "TAG_WAVEFORM_DETAIL", "TAG_PATH"]

MAGIC = b"PMAI"

#: Tags we can serve. ``research/05`` §5.3 catalogues the rest.
TAG_PATH = b"PPTH"              # track file path, UTF-16BE
TAG_BEAT_GRID = b"PQTZ"         # beat grid (.DAT)
TAG_WAVEFORM_PREVIEW = b"PWAV"  # small preview waveform (.DAT)
TAG_WAVEFORM_TINY = b"PWV2"
TAG_WAVEFORM_DETAIL = b"PWV3"   # scrolling waveform (.EXT)
TAG_WAVEFORM_COLOUR = b"PWV4"
TAG_CUES = b"PCOB"              # cues and loops (.DAT)
TAG_CUES_EXT = b"PCO2"          # nxs2 cues (.EXT)


@dataclass(frozen=True)
class AnlzTag:
    """One tag: its identifier and the bytes that follow the header."""

    fourcc: bytes
    header_size: int
    total_size: int
    #: The whole tag including its header, which is what a player expects to
    #: receive -- the dbserver binary responses carry the tag verbatim.
    raw: bytes

    @property
    def payload(self) -> bytes:
        return self.raw[self.header_size :]


class AnlzFile:
    """A parsed ANLZ container."""

    def __init__(self, data: bytes) -> None:
        if len(data) < 0x1C or data[:4] != MAGIC:
            raise DecodeError(
                f"not an ANLZ file: magic {data[:4]!r} != {MAGIC!r}"
            )
        self.data = data
        header_size, file_size = struct.unpack_from(">II", data, 4)

        self.tags: list[AnlzTag] = []
        offset = header_size
        # Stop at the declared file size, not the buffer length: some files
        # carry trailing slack that is not a tag.
        end = min(file_size, len(data)) if file_size else len(data)
        while offset + 12 <= end:
            fourcc = data[offset : offset + 4]
            tag_header, tag_total = struct.unpack_from(">II", data, offset + 4)
            if tag_total < 12 or offset + tag_total > end:
                break  # truncated or corrupt; keep what we have
            self.tags.append(
                AnlzTag(
                    fourcc=fourcc,
                    header_size=tag_header,
                    total_size=tag_total,
                    raw=data[offset : offset + tag_total],
                )
            )
            offset += tag_total

    def tag(self, fourcc: bytes) -> AnlzTag | None:
        for candidate in self.tags:
            if candidate.fourcc == fourcc:
                return candidate
        return None

    def tag_payload(self, fourcc: bytes) -> bytes:
        """The payload of *fourcc*, or empty if this file has no such tag.

        Empty rather than raising: a track analysed by an older rekordbox
        legitimately lacks the newer tags, and a missing waveform should cost
        the waveform rather than the load.
        """
        found = self.tag(fourcc)
        return found.payload if found is not None else b""

    @property
    def fourccs(self) -> list[str]:
        return [t.fourcc.decode("ascii", errors="replace") for t in self.tags]

    def __repr__(self) -> str:  # pragma: no cover - debugging aid
        return f"<AnlzFile {len(self.data)}B tags={self.fourccs}>"
