"""Synthetic ``export.pdb`` construction, shared by the parser and dbserver tests.

Deliberately minimal and *not* a general pdb writer -- it only has to be correct
enough to exercise the reader and to give the dbserver something to serve. A
real writer is what objective 2 eventually needs to expose a Mixxx library to
CDJs, and that is Phase C work.

The layout mirrors a real file: page 0 is the header, then each table gets a
"strange" chain-head page followed by its data page, so the reader's chain
walking and strange-page skipping are both exercised rather than bypassed.
"""

from __future__ import annotations

import struct

from prolinks_poc.proto.pdb import ENTRIES_START, GROUP_SIZE, PAGE_SIZE, PageType
from prolinks_poc.proto.piostring import encode_piostring

__all__ = [
    "PdbBuilder",
    "track_row",
    "artist_row",
    "id_name_row",
    "key_row",
    "playlist_row",
    "playlist_entry_row",
    "sample_library_bytes",
]


class PdbBuilder:
    """Builds a minimal but structurally valid ``export.pdb``.

    Layout: page 0 is the file header, then per table a "strange" chain-head
    page followed by however many data pages its rows need — mirroring how
    real files are arranged, so the reader's chain walking, strange-page
    skipping and multi-page tables are all exercised rather than bypassed.

    Rows are packed greedily. A 4096-byte page has to hold the 40-byte header,
    the row data, *and* a reverse index costing 2 bytes per row plus 4 per
    group of 16 — so a table of any size spills across pages, exactly as it
    does on real media.
    """

    def __init__(self) -> None:
        self.tables: dict[int, list[bytes]] = {}

    def add(self, page_type: int, row: bytes) -> None:
        self.tables.setdefault(page_type, []).append(row)

    @staticmethod
    def _paginate(rows: list[bytes]) -> list[list[bytes]]:
        """Split *rows* into page-sized runs."""
        pages: list[list[bytes]] = []
        current: list[bytes] = []
        used = 0
        for row in rows:
            count = len(current) + 1
            index_cost = ((count + 15) // 16) * GROUP_SIZE
            if current and ENTRIES_START + used + len(row) + index_cost > PAGE_SIZE:
                pages.append(current)
                current, used = [], 0
            current.append(row)
            used += len(row)
        pages.append(current)
        return pages

    def build(self) -> bytes:
        page_types = sorted(self.tables)
        layout: dict[int, list[list[bytes]]] = {
            page_type: self._paginate(self.tables[page_type]) for page_type in page_types
        }

        # Allocate page indices: page 0 is the header, then per table a strange
        # page followed by its data pages.
        first_pages: dict[int, int] = {}
        next_index = 1
        for page_type in page_types:
            first_pages[page_type] = next_index
            next_index += 1 + len(layout[page_type])
        total_pages = next_index

        out = bytearray(PAGE_SIZE * total_pages)
        struct.pack_into("<II", out, 0x04, PAGE_SIZE, len(page_types))
        for i, page_type in enumerate(page_types):
            first = first_pages[page_type]
            last = first + len(layout[page_type])
            struct.pack_into("<IIII", out, 0x1C + i * 16, page_type, 0, first, last)

        for page_type in page_types:
            strange = first_pages[page_type]
            pages = layout[page_type]
            # Strange page: u5 bit 0x40 set, u9 == 1004, links to the first
            # real data page and holds no rows itself.
            self._write_header(out, strange, page_type, strange + 1, 0, u5=0x40, u9=1004)
            for offset, rows in enumerate(pages):
                index = strange + 1 + offset
                is_last = offset == len(pages) - 1
                # The chain terminates by pointing past the end of the file.
                next_page = total_pages if is_last else index + 1
                self._write_data_page(out, index, page_type, rows, next_page)

        return bytes(out)

    @staticmethod
    def _write_header(out, index, page_type, next_index, entry_count, u5=0, u9=0):
        base = index * PAGE_SIZE
        struct.pack_into("<IIII", out, base + 0x04, index, page_type, next_index, 0)
        struct.pack_into("<BBBB", out, base + 0x18, entry_count, 0, 0, u5)
        struct.pack_into("<HHHHHH", out, base + 0x1C, 0, 0, 0, 0, u9, 0)

    def _write_data_page(self, out, index, page_type, rows, next_index):
        base = index * PAGE_SIZE
        self._write_header(out, index, page_type, next_index, len(rows))

        cursor = ENTRIES_START
        offsets = []
        for row in rows:
            offsets.append(cursor - ENTRIES_START)
            out[base + cursor : base + cursor + len(row)] = row
            cursor += len(row)

        page_end = base + PAGE_SIZE
        for group in range((len(rows) + 15) // 16):
            group_base = page_end - group * GROUP_SIZE
            block = group_base - GROUP_SIZE
            present = 0
            for slot in range(16):
                row_index = group * 16 + slot
                if row_index >= len(offsets):
                    break
                present |= 1 << slot
                # Slots run backwards within the group.
                position = block + (15 - slot) * 2
                struct.pack_into("<H", out, position, offsets[row_index])
            struct.pack_into("<H", out, group_base - 4, present)


def track_row(track_id: int, title: str, artist_id: int, bpm_100: int, path: str,
               analyze_path: str = "/PIONEER/USBANLZ/P001/00001/ANLZ0000.DAT",
               file_type: int = 1, disc_number: int = 1) -> bytes:
    """A track row: fixed part, 21-entry string offset table, then the strings."""
    row = bytearray(0x5E + 21 * 2)
    struct.pack_into("<H", row, 0x00, 0x24)  # magic
    struct.pack_into("<I", row, 0x08, 44100)  # sample_rate
    struct.pack_into("<I", row, 0x30, 320)  # bitrate
    struct.pack_into("<I", row, 0x38, bpm_100)
    struct.pack_into("<I", row, 0x44, artist_id)
    struct.pack_into("<I", row, 0x48, track_id)
    struct.pack_into("<H", row, 0x54, 245)  # duration
    row[0x59] = 4  # rating
    struct.pack_into("<H", row, 0x4C, disc_number)
    struct.pack_into("<H", row, 0x5A, file_type)  # container; see pdb.FileType

    strings = {14: analyze_path, 17: title, 19: path.rsplit("/", 1)[-1], 20: path}
    blob = bytearray()
    for index in range(21):
        offset = len(row) + len(blob) if index in strings else 0
        struct.pack_into("<H", row, 0x5E + index * 2, offset)
        if index in strings:
            blob += encode_piostring(strings[index])
    return bytes(row + blob)


def artist_row(artist_id: int, name: str) -> bytes:
    """Short form, magic ``0x60``: name offset is a single byte at 0x09."""
    row = bytearray(10)
    struct.pack_into("<HHI", row, 0, 0x60, 0, artist_id)
    row[0x09] = 10
    return bytes(row) + encode_piostring(name)


def id_name_row(row_id: int, name: str) -> bytes:
    return struct.pack("<I", row_id) + encode_piostring(name)


def playlist_row(playlist_id: int, name: str, parent_id: int, is_folder: bool) -> bytes:
    return (
        struct.pack("<IIIII", parent_id, 0, playlist_id, playlist_id, int(is_folder))
        + encode_piostring(name)
    )


def playlist_entry_row(entry_index: int, track_id: int, playlist_id: int) -> bytes:
    return struct.pack("<III", entry_index, track_id, playlist_id)


def key_row(key_id: int, name: str) -> bytes:
    """``[id:4][id2:4][name:PioString]`` -- id2 duplicates id."""
    return struct.pack("<II", key_id, key_id) + encode_piostring(name)


def sample_library_bytes() -> bytes:
    """A small database with enough shape to exercise both readers.

    Includes a CJK title and artist, a folder containing a playlist, and
    playlist entries written out of order so the ``entry_index`` sort is
    actually tested rather than accidentally satisfied.
    """
    builder = PdbBuilder()
    builder.add(PageType.ARTISTS, artist_row(1, "New Order"))
    builder.add(PageType.ARTISTS, artist_row(2, "夜のテーマ"))
    builder.add(PageType.GENRES, id_name_row(7, "Techno"))
    builder.add(PageType.KEYS, key_row(3, "8A"))
    builder.add(PageType.TRACKS, track_row(101, "Blue Monday", 1, 13000, "/Contents/blue.mp3"))
    builder.add(PageType.TRACKS, track_row(102, "Temptation", 1, 12800, "/Contents/temp.mp3"))
    builder.add(PageType.TRACKS, track_row(103, "夜", 2, 14000, "/Contents/yoru.flac"))
    builder.add(PageType.PLAYLIST_TREE, playlist_row(10, "Sets", 0, True))
    builder.add(PageType.PLAYLIST_TREE, playlist_row(11, "Friday", 10, False))
    builder.add(PageType.PLAYLIST_ENTRIES, playlist_entry_row(2, 102, 11))
    builder.add(PageType.PLAYLIST_ENTRIES, playlist_entry_row(1, 101, 11))
    return builder.build()
