"""``export.pdb`` — the DeviceSQL database rekordbox writes to media.

A fixed-page store: 4096-byte pages, one chain per table, rows written forward
from the page header while their offsets live in a **reverse index** growing
backwards from the end of the page. Everything is little-endian, which is the
opposite of every other format in this project.

Implemented from ``research/05``. The awkward parts, all of which are real and
all of which bite:

* **The reverse index is doubly reversed.** Row offsets are stored in groups of
  16 at the end of the page, and within a group they run backwards -- row 0
  occupies the *last* slot. Each group carries a 16-bit presence bitmask, and
  only the rows whose bit is set exist.
* **The last group's presence bits describe rows that are not there.** Rows
  have to be bounded by the page's entry count, not by the bitmask alone.
* **The entry count is in one of two fields.** ``entry_count_large`` wins when
  it exceeds ``entry_count_small`` -- except when it is the sentinel 8191, and
  except on "strange" pages. Artwork and playlist-map pages are the ones that
  actually need the large field.
* **Every table chain starts with a "strange" page** that holds no rows and
  only links onward.

Random access is required throughout (page indices are absolute), so the whole
file has to be resident. That is equally true of the Kaitai parser Mixxx
already ships, which is why the C++ side will parse from the cached file.
"""

from __future__ import annotations

import enum
import struct
from dataclasses import dataclass, field
from typing import Iterator

from .errors import DecodeError
from .piostring import read_piostring

__all__ = ["PageType", "Pdb", "Row", "PAGE_SIZE"]

PAGE_SIZE = 4096
#: Offset of the first row within a data page; also the page-header length.
ENTRIES_START = 0x28
#: Bytes per reverse-index group: 16 offsets + presence + override.
GROUP_SIZE = 0x24
ROWS_PER_GROUP = 16
#: ``entry_count_large`` uses this as "not meaningful".
ENTRY_COUNT_SENTINEL = 8191


class PageType(enum.IntEnum):
    """Table identifiers (``research/05`` §2.3)."""

    TRACKS = 0
    GENRES = 1
    ARTISTS = 2
    ALBUMS = 3
    LABELS = 4
    KEYS = 5
    COLORS = 6
    PLAYLIST_TREE = 7
    PLAYLIST_ENTRIES = 8
    ARTWORK = 13
    COLUMNS = 16
    HISTORY = 19


@dataclass
class Row:
    """One decoded row. ``fields`` holds the table-specific values."""

    page_type: int
    fields: dict

    def __getitem__(self, key: str):
        return self.fields[key]

    def get(self, key: str, default=None):
        return self.fields.get(key, default)


@dataclass
class _PageHeader:
    index: int
    page_type: int
    next_index: int
    entry_count_small: int
    u5: int
    entry_count_large: int
    u9: int

    @property
    def is_strange(self) -> bool:
        """A chain-head page that links onward but holds no rows."""
        return self.index != 0 and bool(self.u5 & 0x40)

    @property
    def is_empty(self) -> bool:
        return self.index == 0 and self.u9 == 0

    @property
    def entry_count(self) -> int:
        if (
            self.entry_count_small < self.entry_count_large
            and self.entry_count_large != ENTRY_COUNT_SENTINEL
            and not self.is_strange
            and not self.is_empty
        ):
            return self.entry_count_large
        return self.entry_count_small


class Pdb:
    """A parsed ``export.pdb``.

    Rows are decoded lazily per table, so opening a database is cheap and only
    the tables actually asked for are walked.
    """

    def __init__(self, data: bytes) -> None:
        self.data = data
        if len(data) < PAGE_SIZE:
            raise DecodeError(f"export.pdb is only {len(data)} bytes; need at least one page")

        page_size, page_entries = struct.unpack_from("<II", data, 0x04)
        if page_size != PAGE_SIZE:
            raise DecodeError(f"unexpected page size {page_size}; expected {PAGE_SIZE}")
        self.page_size = page_size
        self.page_entries = page_entries

        #: table type -> first page index
        self.tables: dict[int, int] = {}
        for i in range(page_entries):
            offset = 0x1C + i * 16
            if offset + 16 > len(data):
                break
            page_type, _empty, first_page, _last_page = struct.unpack_from(
                "<IIII", data, offset
            )
            # Later duplicates would shadow earlier ones; keep the first.
            self.tables.setdefault(page_type, first_page)

        self._cache: dict[int, list[Row]] = {}

    # -- pages -----------------------------------------------------------

    def _read_header(self, index: int) -> _PageHeader | None:
        start = index * PAGE_SIZE
        if start + ENTRIES_START > len(self.data):
            return None
        (page_index, page_type, next_index, _u1) = struct.unpack_from(
            "<IIII", self.data, start + 0x04
        )
        entry_count_small, _u3, _u4, u5 = struct.unpack_from("<BBBB", self.data, start + 0x18)
        (_free, _payload, _overridden, entry_count_large, u9, _u10) = struct.unpack_from(
            "<HHHHHH", self.data, start + 0x1C
        )
        return _PageHeader(
            index=page_index,
            page_type=page_type,
            next_index=next_index,
            entry_count_small=entry_count_small,
            u5=u5,
            entry_count_large=entry_count_large,
            u9=u9,
        )

    def _row_offsets(self, page_index: int, header: _PageHeader) -> Iterator[int]:
        """Yield absolute file offsets of the live rows on this page.

        The reverse index sits at the end of the page in 36-byte groups. Within
        a group the 16 offsets are stored backwards, so slot ``15 - i`` holds
        row ``i``; a 16-bit bitmask says which are live.
        """
        page_start = page_index * PAGE_SIZE
        page_end = page_start + PAGE_SIZE
        if page_end > len(self.data):
            return

        count = header.entry_count
        groups = (count + ROWS_PER_GROUP - 1) // ROWS_PER_GROUP
        emitted = 0

        for group in range(groups):
            base = page_end - group * GROUP_SIZE
            block = base - GROUP_SIZE
            if block < page_start + ENTRIES_START:
                return
            (present,) = struct.unpack_from("<H", self.data, base - 4)
            for slot in range(ROWS_PER_GROUP):
                if emitted >= count:
                    return
                # Slot ordering is reversed within the group.
                position = block + (ROWS_PER_GROUP - 1 - slot) * 2
                (row_offset,) = struct.unpack_from("<H", self.data, position)
                emitted += 1
                if not (present >> slot) & 1:
                    # A cleared bit is a deleted row, which is normal.
                    continue
                absolute = page_start + ENTRIES_START + row_offset
                if page_start <= absolute < page_end:
                    yield absolute

    def _walk(self, page_type: int) -> Iterator[tuple[int, _PageHeader]]:
        """Follow a table's page chain, guarding against loops."""
        index = self.tables.get(page_type)
        if index is None:
            return
        seen: set[int] = set()
        while index is not None and index not in seen:
            seen.add(index)
            header = self._read_header(index)
            if header is None:
                return
            if not header.is_strange and not header.is_empty:
                yield index, header
            next_index = header.next_index
            # The chain terminates by pointing at or past the end of the file,
            # which is normal rather than corruption.
            if next_index == index or next_index * PAGE_SIZE >= len(self.data):
                return
            index = next_index

    # -- rows ------------------------------------------------------------

    def rows(self, page_type: int) -> list[Row]:
        """Every live row of one table, decoded."""
        if page_type in self._cache:
            return self._cache[page_type]

        decoder = _DECODERS.get(page_type)
        out: list[Row] = []
        if decoder is not None:
            for page_index, header in self._walk(page_type):
                for offset in self._row_offsets(page_index, header):
                    try:
                        fields = decoder(self.data, offset)
                    except (DecodeError, struct.error, IndexError):
                        # One malformed row must not cost the whole table.
                        continue
                    if fields is not None:
                        out.append(Row(page_type=page_type, fields=fields))
        self._cache[page_type] = out
        return out

    def table_summary(self) -> dict[str, int]:
        """Row counts per table, for ``prolinks pdb-dump``."""
        summary = {}
        for page_type in sorted(self.tables):
            try:
                name = PageType(page_type).name
            except ValueError:
                continue
            if page_type in _DECODERS:
                summary[name] = len(self.rows(page_type))
        return summary


# -- row decoders ----------------------------------------------------------
#
# Offsets come straight from research/05 §3. Everything is little-endian.


#: The 21 indexed strings on a track row. Only the named ones matter; the rest
#: are recorded as unknowns in the research doc and skipped here.
TRACK_STRINGS = {
    6: "kuvo_public",
    7: "autoload_hotcues",
    10: "date_added",
    11: "release_date",
    12: "mix_name",
    14: "analyze_path",
    15: "analyze_date",
    16: "comment",
    17: "title",
    19: "filename",
    20: "path",
}

TRACK_MAGIC = 0x24


def _decode_track(data: bytes, offset: int) -> dict | None:
    (magic,) = struct.unpack_from("<H", data, offset)
    if magic != TRACK_MAGIC:
        return None
    (
        sample_rate, composer_id, file_size, _u1,
    ) = struct.unpack_from("<IIII", data, offset + 0x08)
    (
        artwork_id, key_id, original_artist_id, label_id, remixer_id,
        bitrate, track_number, bpm_100, genre_id, album_id, artist_id, track_id,
    ) = struct.unpack_from("<IIIIIIIIIIII", data, offset + 0x1C)
    (
        disc_number, play_count, year, sample_depth, duration, _u4,
    ) = struct.unpack_from("<HHHHHH", data, offset + 0x4C)
    color_id, rating = data[offset + 0x58], data[offset + 0x59]

    fields = {
        "id": track_id,
        "title": "",
        "artist_id": artist_id,
        "album_id": album_id,
        "genre_id": genre_id,
        "key_id": key_id,
        "label_id": label_id,
        "artwork_id": artwork_id,
        "composer_id": composer_id,
        "original_artist_id": original_artist_id,
        "remixer_id": remixer_id,
        "bpm_100": bpm_100,
        "duration": duration,
        "bitrate": bitrate,
        "sample_rate": sample_rate,
        "sample_depth": sample_depth,
        "file_size": file_size,
        "track_number": track_number,
        "disc_number": disc_number,
        "play_count": play_count,
        "year": year,
        "rating": rating,
        "color_id": color_id,
    }
    for index, name in TRACK_STRINGS.items():
        position = offset + 0x5E + index * 2
        (relative,) = struct.unpack_from("<H", data, position)
        try:
            fields[name] = read_piostring(data, offset + relative)
        except DecodeError:
            fields[name] = ""
    return fields


def _decode_artist(data: bytes, offset: int) -> dict | None:
    """Magic ``0x60`` short form or ``0x64`` long form (``research/05`` §3.3)."""
    (magic, _index_shift, artist_id) = struct.unpack_from("<HHI", data, offset)
    if magic == 0x60:
        name_offset = data[offset + 0x09]
    elif magic == 0x64:
        (name_offset,) = struct.unpack_from("<H", data, offset + 0x0A)
    else:
        return None
    return {"id": artist_id, "name": read_piostring(data, offset + name_offset)}


def _decode_album(data: bytes, offset: int) -> dict | None:
    (magic,) = struct.unpack_from("<H", data, offset)
    if magic != 0x80:
        return None
    (album_artist_id, album_id) = struct.unpack_from("<II", data, offset + 0x08)
    name_offset = data[offset + 0x15]
    return {
        "id": album_id,
        "album_artist_id": album_artist_id,
        "name": read_piostring(data, offset + name_offset),
    }


def _decode_id_name(data: bytes, offset: int) -> dict:
    """Genres and labels: ``[id:4][name:PioString]``."""
    (row_id,) = struct.unpack_from("<I", data, offset)
    return {"id": row_id, "name": read_piostring(data, offset + 4)}


def _decode_key(data: bytes, offset: int) -> dict:
    """``[id:4][id2:4][name:PioString]`` -- id2 duplicates id."""
    (row_id,) = struct.unpack_from("<I", data, offset)
    return {"id": row_id, "name": read_piostring(data, offset + 8)}


def _decode_color(data: bytes, offset: int) -> dict:
    """``[pad:4][id_dup:1][id:1][pad:2][name:PioString]``.

    The id is one byte, matching the track row's one-byte ``color_id``.
    """
    return {"id": data[offset + 5], "name": read_piostring(data, offset + 8)}


def _decode_artwork(data: bytes, offset: int) -> dict:
    (row_id,) = struct.unpack_from("<I", data, offset)
    return {"id": row_id, "path": read_piostring(data, offset + 4)}


def _decode_playlist(data: bytes, offset: int) -> dict:
    (parent_id, _pad, sort_order, playlist_id, is_folder) = struct.unpack_from(
        "<IIIII", data, offset
    )
    return {
        "id": playlist_id,
        "parent_id": parent_id,
        "sort_order": sort_order,
        "is_folder": bool(is_folder),
        "name": read_piostring(data, offset + 0x14),
    }


def _decode_playlist_entry(data: bytes, offset: int) -> dict:
    (entry_index, track_id, playlist_id) = struct.unpack_from("<III", data, offset)
    return {"entry_index": entry_index, "track_id": track_id, "playlist_id": playlist_id}


_DECODERS = {
    PageType.TRACKS: _decode_track,
    PageType.ARTISTS: _decode_artist,
    PageType.ALBUMS: _decode_album,
    PageType.GENRES: _decode_id_name,
    PageType.LABELS: _decode_id_name,
    PageType.KEYS: _decode_key,
    PageType.COLORS: _decode_color,
    PageType.ARTWORK: _decode_artwork,
    PageType.PLAYLIST_TREE: _decode_playlist,
    PageType.PLAYLIST_ENTRIES: _decode_playlist_entry,
}
