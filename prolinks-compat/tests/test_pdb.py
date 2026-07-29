"""``export.pdb`` parser tests, driven by a synthetic database.

No real `export.pdb` is committed (they are large and contain someone's music
library), so these build one. That is worth doing rather than waiting for
hardware: the page format's awkward parts — the doubly-reversed row index, the
presence bitmask, the two competing entry-count fields — are exactly the things
that fail silently, and finding out on a USB stick at midnight is the expensive
way to learn.

The builder here is deliberately minimal and *not* a general pdb writer. A real
one is needed eventually to serve a Mixxx library to CDJs, but that is Phase C;
this only has to be correct enough to exercise the reader.
"""

from __future__ import annotations

import struct

import pytest

from prolinks_poc.core.library import Library
from prolinks_poc.proto.errors import DecodeError
from prolinks_poc.proto.pdb import ENTRIES_START, GROUP_SIZE, PAGE_SIZE, PageType, Pdb
from prolinks_poc.proto.piostring import encode_piostring, read_piostring


# -- PioString -------------------------------------------------------------


@pytest.mark.parametrize(
    "text",
    ["", "a", "Blue Monday", "x" * 100, "x" * 200, "Étude", "夜のテーマ", "Ø±"],
    ids=["empty", "one", "short", "long-ascii-100", "long-ascii-200", "accented", "cjk", "mixed"],
)
def test_piostring_round_trip(text):
    assert read_piostring(encode_piostring(text), 0) == text


def test_short_ascii_length_arithmetic():
    """``research/05`` §4: the length byte is ``2 * (len + 1) + 1``."""
    encoded = encode_piostring("abc")
    assert encoded[0] == 2 * (3 + 1) + 1 == 9
    assert encoded[1:] == b"abc"
    # ...and decoding inverts it: (9 - 1) // 2 - 1 == 3.
    assert read_piostring(encoded, 0) == "abc"


def test_long_ascii_uses_the_0x40_selector_with_a_plus_four_length():
    encoded = encode_piostring("y" * 200)
    assert encoded[0] == 0x40
    assert struct.unpack_from("<H", encoded, 1)[0] == 200 + 4
    assert encoded[3] == 0  # padding byte
    assert read_piostring(encoded, 0) == "y" * 200


def test_utf16_uses_the_0x90_selector_and_big_endian_text():
    """Big-endian here, unlike the little-endian strings in the NFS layer."""
    encoded = encode_piostring("夜")
    assert encoded[0] == 0x90
    assert struct.unpack_from("<H", encoded, 1)[0] == 2 + 4
    assert encoded[3:] == "夜".encode("utf-16-be")


def test_piostring_rejects_a_length_running_past_the_buffer():
    truncated = bytes([0x40]) + struct.pack("<H", 1000 + 4) + b"\x00" + b"short"
    with pytest.raises(DecodeError, match="claims 1000 bytes"):
        read_piostring(truncated, 0)


# -- synthetic database ----------------------------------------------------


class PdbBuilder:
    """Builds a minimal but structurally valid ``export.pdb``.

    Layout: page 0 is the file header, then one "strange" chain-head page and
    one data page per table — mirroring how real files are arranged, so the
    reader's chain walking and strange-page skipping are both exercised.
    """

    def __init__(self) -> None:
        self.tables: dict[int, list[bytes]] = {}

    def add(self, page_type: int, row: bytes) -> None:
        self.tables.setdefault(page_type, []).append(row)

    def build(self) -> bytes:
        page_types = sorted(self.tables)
        # Page 0 is the header; each table then gets a strange page followed by
        # its data page.
        first_pages = {pt: 1 + 2 * i for i, pt in enumerate(page_types)}
        total_pages = 1 + 2 * len(page_types)

        out = bytearray(PAGE_SIZE * total_pages)
        struct.pack_into("<II", out, 0x04, PAGE_SIZE, len(page_types))
        for i, page_type in enumerate(page_types):
            first = first_pages[page_type]
            struct.pack_into(
                "<IIII", out, 0x1C + i * 16, page_type, 0, first, first + 1
            )

        for page_type in page_types:
            strange = first_pages[page_type]
            data_page = strange + 1
            # Strange page: u5 bit 0x40 set, u9 == 1004, links onward.
            self._write_header(out, strange, page_type, data_page, 0, u5=0x40, u9=1004)
            self._write_data_page(out, data_page, page_type, self.tables[page_type])

        return bytes(out)

    @staticmethod
    def _write_header(out, index, page_type, next_index, entry_count, u5=0, u9=0):
        base = index * PAGE_SIZE
        struct.pack_into("<IIII", out, base + 0x04, index, page_type, next_index, 0)
        struct.pack_into("<BBBB", out, base + 0x18, entry_count, 0, 0, u5)
        struct.pack_into("<HHHHHH", out, base + 0x1C, 0, 0, 0, 0, u9, 0)

    def _write_data_page(self, out, index, page_type, rows):
        base = index * PAGE_SIZE
        # next_index past the end of the file terminates the chain, which is
        # how real files end their chains.
        self._write_header(out, index, page_type, 0xFFFF, len(rows))

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


def _track_row(track_id: int, title: str, artist_id: int, bpm_100: int, path: str,
               analyze_path: str = "/PIONEER/USBANLZ/P001/00001/ANLZ0000.DAT") -> bytes:
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

    strings = {14: analyze_path, 17: title, 19: path.rsplit("/", 1)[-1], 20: path}
    blob = bytearray()
    for index in range(21):
        offset = len(row) + len(blob) if index in strings else 0
        struct.pack_into("<H", row, 0x5E + index * 2, offset)
        if index in strings:
            blob += encode_piostring(strings[index])
    return bytes(row + blob)


def _artist_row(artist_id: int, name: str) -> bytes:
    """Short form, magic ``0x60``: name offset is a single byte at 0x09."""
    row = bytearray(10)
    struct.pack_into("<HHI", row, 0, 0x60, 0, artist_id)
    row[0x09] = 10
    return bytes(row) + encode_piostring(name)


def _id_name_row(row_id: int, name: str) -> bytes:
    return struct.pack("<I", row_id) + encode_piostring(name)


def _playlist_row(playlist_id: int, name: str, parent_id: int, is_folder: bool) -> bytes:
    return (
        struct.pack("<IIIII", parent_id, 0, playlist_id, playlist_id, int(is_folder))
        + encode_piostring(name)
    )


def _playlist_entry_row(entry_index: int, track_id: int, playlist_id: int) -> bytes:
    return struct.pack("<III", entry_index, track_id, playlist_id)


@pytest.fixture
def sample_pdb() -> bytes:
    builder = PdbBuilder()
    builder.add(PageType.ARTISTS, _artist_row(1, "New Order"))
    builder.add(PageType.ARTISTS, _artist_row(2, "夜のテーマ"))
    builder.add(PageType.GENRES, _id_name_row(7, "Techno"))
    builder.add(PageType.KEYS, struct.pack("<II", 3, 3) + encode_piostring("8A"))
    builder.add(PageType.TRACKS, _track_row(101, "Blue Monday", 1, 13000, "/Contents/a/blue.mp3"))
    builder.add(PageType.TRACKS, _track_row(102, "Temptation", 1, 12800, "/Contents/a/temp.mp3"))
    builder.add(PageType.TRACKS, _track_row(103, "夜", 2, 14000, "/Contents/b/yoru.flac"))
    builder.add(PageType.PLAYLIST_TREE, _playlist_row(10, "Sets", 0, True))
    builder.add(PageType.PLAYLIST_TREE, _playlist_row(11, "Friday", 10, False))
    builder.add(PageType.PLAYLIST_ENTRIES, _playlist_entry_row(2, 102, 11))
    builder.add(PageType.PLAYLIST_ENTRIES, _playlist_entry_row(1, 101, 11))
    return builder.build()


def test_file_header_is_parsed(sample_pdb):
    pdb = Pdb(sample_pdb)
    assert pdb.page_size == PAGE_SIZE
    assert PageType.TRACKS in pdb.tables


def test_strange_pages_contribute_no_rows(sample_pdb):
    """Every chain starts with one; it must be walked through, not read."""
    pdb = Pdb(sample_pdb)
    assert len(pdb.rows(PageType.TRACKS)) == 3


def test_track_rows_decode(sample_pdb):
    pdb = Pdb(sample_pdb)
    tracks = {row["id"]: row for row in pdb.rows(PageType.TRACKS)}
    assert set(tracks) == {101, 102, 103}
    assert tracks[101]["title"] == "Blue Monday"
    assert tracks[101]["bpm_100"] == 13000
    assert tracks[101]["path"] == "/Contents/a/blue.mp3"
    assert tracks[101]["sample_rate"] == 44100
    assert tracks[101]["rating"] == 4
    assert tracks[103]["title"] == "夜", "UTF-16BE titles must survive"


def test_library_resolves_foreign_keys(sample_pdb):
    library = Library.from_bytes(sample_pdb)
    track = library.tracks[101]
    assert track.artist == "New Order"
    assert track.bpm == 130.0
    assert track.duration_text == "4:05"
    assert library.tracks[103].artist == "夜のテーマ"


def test_analyze_ext_path_swaps_the_extension(sample_pdb):
    library = Library.from_bytes(sample_pdb)
    track = library.tracks[101]
    assert track.analyze_path.endswith("ANLZ0000.DAT")
    assert track.analyze_ext_path.endswith("ANLZ0000.EXT")


def test_playlist_tree_and_ordering(sample_pdb):
    """Entries carry an explicit index; on-disk order is not playlist order."""
    library = Library.from_bytes(sample_pdb)
    friday = library.playlists[11]
    assert friday.name == "Friday"
    assert friday.parent_id == 10
    assert not friday.is_folder
    # Rows were added 102-then-101, but entry_index says 101 comes first.
    assert friday.track_ids == [101, 102]
    assert [t.title for t in library.playlist_tracks(11)] == ["Blue Monday", "Temptation"]

    folder = library.playlists[10]
    assert folder.is_folder
    assert [child.id for child in folder.children] == [11]


def test_summary_and_search(sample_pdb):
    library = Library.from_bytes(sample_pdb)
    assert library.summary()["tracks"] == 3
    assert library.summary()["folders"] == 1
    assert [t.id for t in library.search("monday")] == [101]


def test_row_index_survives_more_than_one_group():
    """Groups hold 16 rows; a 17th forces a second group and exercises the
    backwards-walking index arithmetic that is easy to get subtly wrong."""
    builder = PdbBuilder()
    for i in range(40):
        builder.add(PageType.GENRES, _id_name_row(i + 1, f"Genre {i}"))
    pdb = Pdb(builder.build())
    rows = pdb.rows(PageType.GENRES)
    assert len(rows) == 40
    assert {row["id"] for row in rows} == set(range(1, 41))
    assert {row["name"] for row in rows} == {f"Genre {i}" for i in range(40)}


def test_deleted_rows_are_skipped():
    """A cleared presence bit means the slot is dead, not that the page ends."""
    builder = PdbBuilder()
    for i in range(4):
        builder.add(PageType.GENRES, _id_name_row(i + 1, f"Genre {i}"))
    data = bytearray(builder.build())

    # Clear the bit for row 1 (slot 1) in the genres data page's first group.
    page = 1 + 2 * sorted(builder.tables).index(PageType.GENRES) + 1
    flags_at = page * PAGE_SIZE + PAGE_SIZE - 4
    (present,) = struct.unpack_from("<H", data, flags_at)
    struct.pack_into("<H", data, flags_at, present & ~0b0010)

    rows = Pdb(bytes(data)).rows(PageType.GENRES)
    assert {row["id"] for row in rows} == {1, 3, 4}


def test_a_malformed_row_does_not_lose_the_table():
    """One bad row costs that row. Real media has damaged rows."""
    builder = PdbBuilder()
    builder.add(PageType.TRACKS, _track_row(1, "Good", 0, 12000, "/a.mp3"))
    builder.add(PageType.TRACKS, b"\x00\x00" + bytes(40))  # wrong magic
    builder.add(PageType.TRACKS, _track_row(3, "Also good", 0, 12000, "/b.mp3"))
    rows = Pdb(builder.build()).rows(PageType.TRACKS)
    assert {row["id"] for row in rows} == {1, 3}


def test_rejects_a_truncated_file():
    with pytest.raises(DecodeError, match="at least one page"):
        Pdb(b"too short")


def test_rejects_an_unexpected_page_size():
    data = bytearray(PAGE_SIZE)
    struct.pack_into("<II", data, 0x04, 2048, 1)
    with pytest.raises(DecodeError, match="unexpected page size"):
        Pdb(bytes(data))
