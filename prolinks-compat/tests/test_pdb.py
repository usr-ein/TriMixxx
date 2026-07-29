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
from prolinks_poc.proto.pdb import (
    ENTRIES_START,
    GROUP_SIZE,
    PAGE_SIZE,
    PageType,
    Pdb,
    stable_digest,
)
from prolinks_poc.proto.piostring import encode_piostring, read_piostring

from pdb_fixtures import (
    PdbBuilder,
    artist_row,
    id_name_row,
    key_row,
    playlist_entry_row,
    playlist_row,
    sample_library_bytes,
    track_row,
)


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


def test_utf16_uses_the_0x90_selector_with_padding_and_little_endian_text():
    encoded = encode_piostring("夜")
    assert encoded[0] == 0x90
    assert struct.unpack_from("<H", encoded, 1)[0] == 2 + 4
    assert encoded[3] == 0  # padding byte, exactly as in the long-ASCII form
    assert encoded[4:] == "夜".encode("utf-16-le")


# Lifted verbatim from a real ``export.pdb`` (a CDJ-2000NXS's USB stick), with
# the expected text taken from that same medium's *filesystem* -- an
# independent source, which is the point. Encoder/decoder round-trips cannot
# catch this class of bug: the original implementation read UTF-16BE from
# ``offset + 3`` and agreed with its own encoder perfectly, because that is
# byte-identical to UTF-16LE from ``offset + 4`` for any all-ASCII string.
REAL_UTF16_PIOSTRINGS = [
    pytest.param(
        bytes.fromhex(
            "90200000272742005200410049004e00"
            "4400410041004d004100470045002727"
        ),
        "✧BRAINDAAMAGE✧",
        id="sparkles",  # 0x2f6d4 -- the name that failed the load
    ),
    pytest.param(
        bytes.fromhex(
            "903e0000300031002e00200041006b00690062006100200"
            "02d0020005d30573066300130164e4c754b308930e3893e"
            "6555308c305f302e006d0070003300"
        ),
        "01. Akiba - そして、世界から解放された.mp3",
        id="japanese",  # 0x75d24
    ),
    pytest.param(
        bytes.fromhex(
            "9044000030003900 2e0020004100 6b006900720061002000"
            "540061006b0065006d006f0074006f002000142020004b002e00"
            "49002e0044002e0073002e006d0070003300".replace(" ", "")
        ),
        "09. Akira Takemoto — K.I.D.s.mp3",
        id="em-dash",  # 0xac7a0 -- decoded as "\x14†" before the fix
    ),
]


@pytest.mark.parametrize("raw,expected", REAL_UTF16_PIOSTRINGS)
def test_utf16_piostrings_from_a_real_pdb_decode_to_the_names_on_disk(raw, expected):
    """docs/FINDINGS.md O6. Mojibake here served a CDJ a path that does not
    exist, and the resulting ``NFSERR_NOENT`` failed the track load."""
    assert read_piostring(raw, 0) == expected


@pytest.mark.parametrize("raw,expected", REAL_UTF16_PIOSTRINGS)
def test_our_encoder_reproduces_a_real_pdbs_bytes(raw, expected):
    """The other half: what we write must match what rekordbox writes."""
    assert encode_piostring(expected) == raw


def test_piostring_rejects_a_length_running_past_the_buffer():
    truncated = bytes([0x40]) + struct.pack("<H", 1000 + 4) + b"\x00" + b"short"
    with pytest.raises(DecodeError, match="claims 1000 bytes"):
        read_piostring(truncated, 0)


# -- synthetic database ----------------------------------------------------


@pytest.fixture
def sample_pdb() -> bytes:
    return sample_library_bytes()


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
    assert tracks[101]["path"] == "/Contents/blue.mp3"
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
        builder.add(PageType.GENRES, id_name_row(i + 1, f"Genre {i}"))
    pdb = Pdb(builder.build())
    rows = pdb.rows(PageType.GENRES)
    assert len(rows) == 40
    assert {row["id"] for row in rows} == set(range(1, 41))
    assert {row["name"] for row in rows} == {f"Genre {i}" for i in range(40)}


def test_deleted_rows_are_skipped():
    """A cleared presence bit means the slot is dead, not that the page ends."""
    builder = PdbBuilder()
    for i in range(4):
        builder.add(PageType.GENRES, id_name_row(i + 1, f"Genre {i}"))
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
    builder.add(PageType.TRACKS, track_row(1, "Good", 0, 12000, "/a.mp3"))
    builder.add(PageType.TRACKS, b"\x00\x00" + bytes(40))  # wrong magic
    builder.add(PageType.TRACKS, track_row(3, "Also good", 0, 12000, "/b.mp3"))
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


def test_zero_string_offset_is_an_absent_field(sample_pdb):
    """Offset 0 points at the row's own magic, so it cannot be a string.

    Dereferencing it decodes the header as text and yields convincing garbage
    rather than an error, which is the worst kind of bug -- it surfaced as a
    mangled `comment` field only when the data reached a browse UI.
    """
    library = Library.from_bytes(sample_pdb)
    track = library.tracks[101]
    assert track.comment == ""
    assert track.date_added == ""
    # ...while the slots that *were* populated still decode.
    assert track.title == "Blue Monday"
    assert track.path == "/Contents/blue.mp3"


def test_stable_digest_ignores_the_players_write_counter():
    """A CDJ bumps the header's `sequence` field as it operates.

    Pulling the same database over NFS and then reading it off the ejected
    stick produced files differing in exactly two header fields and nowhere
    else. Mixxx keys its media cache on this hash, so a naive whole-file digest
    would re-download and re-parse a whole library because a play count was
    written. See FINDINGS F13.
    """
    import hashlib
    import struct

    original = bytearray(sample_library_bytes())
    touched = bytearray(original)
    # Simulate the player writing: unknown1 4 -> 5, sequence n -> n+1.
    struct.pack_into("<I", touched, 0x10, 5)
    struct.pack_into("<I", touched, 0x14, 20586)

    assert hashlib.sha256(original).digest() != hashlib.sha256(touched).digest()
    assert stable_digest(bytes(original)) == stable_digest(bytes(touched))


def test_stable_digest_still_notices_real_changes():
    """It must only mask the bookkeeping, not actual library content."""
    original = sample_library_bytes()
    builder = PdbBuilder()
    builder.add(PageType.TRACKS, track_row(999, "Different", 0, 12000, "/x.mp3"))
    assert stable_digest(original) != stable_digest(builder.build())


def test_stable_digest_handles_a_runt_file():
    assert stable_digest(b"tiny")  # must not raise on a file shorter than the header


@pytest.mark.parametrize(
    "file_type,name",
    [(1, "MP3"), (4, "AAC"), (5, "FLAC"), (11, "WAV"), (12, "AIFF")],
)
def test_file_type_is_parsed_from_row_offset_0x5a(file_type, name):
    """docs/FINDINGS.md F34.

    Determined by rendering one track into every format a CDJ accepts and
    reading back what rekordbox wrote: 651 rows, no exceptions within a
    container. dysentery's schema leaves the field unnamed.

    It is not cosmetic. A player takes this value at face value, so announcing
    MP3 for a WAV makes it fetch the file, fail to decode it, and say so.
    """
    from prolinks_poc.proto.pdb import FileType

    builder = PdbBuilder()
    builder.add(PageType.TRACKS,
                track_row(1, "T", 1, 12800, "/Contents/a.x", file_type=file_type))
    row = next(iter(Pdb(builder.build()).rows(PageType.TRACKS)))
    assert row["file_type"] == file_type
    assert FileType(row["file_type"]).name == name


def test_disc_number_survives_the_round_trip():
    builder = PdbBuilder()
    builder.add(PageType.TRACKS,
                track_row(1, "T", 1, 12800, "/Contents/a.mp3", disc_number=3))
    assert next(iter(Pdb(builder.build()).rows(PageType.TRACKS)))["disc_number"] == 3
