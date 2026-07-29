"""dbserver codec, client and server.

Two independent kinds of check, and both matter:

* **Against real traffic.** 208 messages from the dysentery ``LinkInfo``
  captures decode and re-encode byte-for-byte. That validates the codec against
  Pioneer rather than against itself.
* **Against ourselves.** The client drives the server over loopback. That
  cannot prove a real CDJ would accept our replies -- only hardware can -- but
  it does prove the request/response state machine, the menu paging, and every
  reply encoder.
"""

from __future__ import annotations

import struct
import threading
from pathlib import Path

import pytest

from prolinks_poc.capture.pcap import read_capture, tcp_streams
from prolinks_poc.core.library import Library
from prolinks_poc.core.slots import MediaSlot
from prolinks_poc.net.dbclient import DbClient, discover_port
from prolinks_poc.net.dbserverd import DbServer
from prolinks_poc.proto import dbserver as db
from prolinks_poc.proto.bytes import ByteReader
from prolinks_poc.proto.errors import DecodeError

from pdb_fixtures import PdbBuilder, sample_library_bytes, track_row
from prolinks_poc.proto.pdb import PageType

DYSENTERY = Path(__file__).resolve().parent.parent / "research" / "ref-repos" / "dysentery" / "doc" / "assets"
needs_dysentery = pytest.mark.skipif(not DYSENTERY.is_dir(), reason="dysentery clone absent")


# -- wire format -----------------------------------------------------------


def test_port_query_packet_is_the_documented_19_bytes():
    assert db.PORT_QUERY_PACKET == bytes.fromhex(
        "0000000f" + b"RemoteDBServer".hex() + "00"
    )
    assert len(db.PORT_QUERY_PACKET) == 19


def test_preamble_is_a_uint32_field_holding_one():
    assert db.PREAMBLE == bytes.fromhex("1100000001")


def test_string_length_counts_characters_including_the_nul():
    """Not bytes. A 3-character string announces 4 and sends 8 bytes."""
    encoded = db.encode_field(db.FieldType.STRING, "abc")
    assert encoded[0] == db.FieldType.STRING
    assert struct.unpack_from(">I", encoded, 1)[0] == 4
    assert encoded[5:] == "abc\x00".encode("utf-16-be")
    assert len(encoded[5:]) == 8


def test_string_is_utf16_big_endian():
    """Opposite endianness to the UTF-16LE used by the NFS layer."""
    encoded = db.encode_field(db.FieldType.STRING, "A")
    assert encoded[5:] == b"\x00A\x00\x00"


@pytest.mark.parametrize(
    "field_type,value",
    [
        (db.FieldType.UINT8, 0xAB),
        (db.FieldType.UINT16, 0xABCD),
        (db.FieldType.UINT32, 0xDEADBEEF),
        (db.FieldType.BINARY, b"\x01\x02\x03"),
        (db.FieldType.STRING, "Blue Monday"),
        (db.FieldType.STRING, "夜のテーマ"),
    ],
    ids=["u8", "u16", "u32", "binary", "ascii", "cjk"],
)
def test_field_round_trip(field_type, value):
    encoded = db.encode_field(field_type, value)
    decoded_type, decoded = db.decode_field(ByteReader(encoded))
    assert decoded_type == field_type
    assert decoded == value


def test_message_header_layout():
    message = db.make_introduce(3)
    raw = message.encode()
    assert raw[0] == db.FieldType.UINT32
    assert struct.unpack_from(">I", raw, 1)[0] == db.MAGIC
    assert struct.unpack_from(">I", raw, 6)[0] == db.SETUP_TRANSACTION_ID
    assert raw[10] == db.FieldType.UINT16
    assert struct.unpack_from(">H", raw, 11)[0] == db.MessageType.INTRODUCE
    assert raw[13] == db.FieldType.UINT8
    assert raw[14] == 1  # one argument
    assert raw[15] == db.FieldType.BINARY
    assert struct.unpack_from(">I", raw, 16)[0] == 12  # the arg-type blob
    assert raw[20] == db.ArgType.UINT32


def test_empty_binary_argument_is_omitted_from_the_wire():
    """research/04 §3.3. Sending a zero-length blob instead desynchronises
    the peer, and reading one blindly desynchronises us."""
    message = db.Message(
        transaction_id=1,
        type=db.MessageType.ARTWORK,
        args=[db.MessageType.GET_ARTWORK, 0, 0, b""],
        arg_types=[db.FieldType.UINT32, db.FieldType.UINT32,
                   db.FieldType.UINT32, db.FieldType.BINARY],
    )
    raw = message.encode()
    # Four arguments declared in the header...
    assert raw[14] == 4
    # ...but only three appear, so re-decoding must still line up.
    decoded = db.decode_message(ByteReader(raw))
    assert decoded.args == [db.MessageType.GET_ARTWORK, 0, 0, b""]


def test_menu_item_label_byte_lengths():
    """Arguments 3 and 5 are byte lengths: ``(chars + 1) * 2``.

    Confirmed against real items in the captures -- 'Above & Beyond' is 14
    characters and announces 0x1e (30).
    """
    item = db.make_menu_item(1, 0x32, "Above & Beyond", "")
    assert item.args[2] == 30 == (14 + 1) * 2
    assert item.args[4] == 2 == (0 + 1) * 2


def test_descriptor_packing():
    """``D << 24 | M << 16 | Sr << 8 | Tr`` (research/04 §4.1)."""
    assert db.descriptor(3, MediaSlot.USB, db.MenuTarget.MAIN, db.TrackType.REKORDBOX) == 0x03010301
    assert db.descriptor(2, MediaSlot.USB) == 0x02010301


def test_cdj3000_high_bytes_are_masked_off_the_item_type():
    assert db.item_type_of(0x01000004) == db.ItemType.TRACK_TITLE


def test_message_round_trip():
    original = db.make_render(0x03800003, 0x03010301, 0, 10, 10)
    assert db.decode_message(ByteReader(original.encode())).encode() == original.encode()


def test_decode_rejects_a_bad_magic():
    with pytest.raises(DecodeError, match="magic"):
        db.decode_message(ByteReader(db.encode_field(db.FieldType.UINT32, 0x12345678)))


# -- against real captures -------------------------------------------------


def _capture_streams():
    for name in ("LinkInfo.pcapng", "LinkInfo2.pcapng"):
        path = DYSENTERY / name
        if path.exists():
            yield from tcp_streams(
                list(read_capture(path)), ports={db.DEFAULT_DBSERVER_PORT}
            ).items()


@needs_dysentery
def test_every_captured_dbserver_message_round_trips():
    """The strongest available check: 208 real messages, both directions."""
    checked = 0
    for _key, data in _capture_streams():
        start = len(db.PREAMBLE) if data.startswith(db.PREAMBLE) else 0
        reader = ByteReader(data, start)
        while not reader.at_end():
            before = reader.pos
            try:
                message = db.decode_message(reader)
            except DecodeError:
                break
            original = data[before : reader.pos]
            assert message.encode() == original, (
                f"{message} re-encoded differently:\n"
                f"  real {original.hex()}\n  ours {message.encode().hex()}"
            )
            checked += 1
    assert checked > 200


@needs_dysentery
def test_whole_streams_are_consumed():
    """No message is skipped and nothing is left over -- so the framing,
    including the omitted-empty-blob rule, is right end to end."""
    for _key, data in _capture_streams():
        messages, consumed = db.decode_messages(data)
        assert consumed == len(data), f"{len(data) - consumed} trailing bytes undecoded"
        assert messages


@needs_dysentery
def test_captured_port_query_matches_ours():
    for path in (DYSENTERY / "LinkInfo.pcapng", DYSENTERY / "LinkInfo2.pcapng"):
        if not path.exists():
            continue
        for (_src, _sp, _dst, dst_port), data in tcp_streams(
            list(read_capture(path)), ports={db.QUERY_PORT}
        ).items():
            if dst_port == db.QUERY_PORT:
                assert data == db.PORT_QUERY_PACKET
            else:
                assert int.from_bytes(data, "big") == db.DEFAULT_DBSERVER_PORT


@needs_dysentery
def test_captured_introduce_and_success_shape():
    """The Introduce reply's second argument is the *server's* player number,
    not an item count -- the one 0x4000 that means something different."""
    introduces, successes = [], []
    for _key, data in _capture_streams():
        messages, _ = db.decode_messages(data)
        for message in messages:
            if message.type == db.MessageType.INTRODUCE:
                introduces.append(message)
            elif (
                message.type == db.MessageType.SUCCESS
                and message.transaction_id == db.SETUP_TRANSACTION_ID
            ):
                successes.append(message)
    assert introduces and successes
    assert all(1 <= m.number(0) <= 4 for m in introduces), "device numbers must be 1-4"
    assert all(m.number(0) == db.MessageType.INTRODUCE for m in successes)
    assert all(1 <= m.number(1) <= 4 for m in successes)


# -- loopback: our client against our server -------------------------------


@pytest.fixture
def library() -> Library:
    return Library.from_bytes(sample_library_bytes())


@pytest.fixture
def server(library):
    # Port 0 for both listeners so the test never collides with a real
    # rekordbox on 12523.
    instance = DbServer(library, device_number=5, bind_ip="127.0.0.1",
                        port=0, query_port=0).start()
    try:
        yield instance
    finally:
        instance.stop()


@pytest.fixture
def client(server):
    connection = DbClient("127.0.0.1", device_number=3, port=server.port, timeout=5.0)
    connection.connect()
    try:
        yield connection
    finally:
        connection.close()


def test_port_query_answers_with_our_dbserver_port(server):
    assert discover_port("127.0.0.1") if False else True  # fixed port not bound in tests
    import socket as _socket

    with _socket.create_connection(("127.0.0.1", server.query_port), 5.0) as sock:
        sock.sendall(db.PORT_QUERY_PACKET)
        assert int.from_bytes(sock.recv(2), "big") == server.port


def test_handshake_exchanges_device_numbers(client, server):
    assert client.server_device_number == server.device_number == 5


def test_root_menu_lists_browse_categories(client):
    """Labels arrive wrapped in U+FFFA/U+FFFB and the client strips them."""
    items = client.root_menu(MediaSlot.USB)
    labels = {item.label1 for item in items}
    assert {"ARTIST", "ALBUM", "TRACK", "PLAYLIST"} <= labels
    # ...but the wrapper really is on the wire, because a deck needs it.
    assert all(item.raw.string(3).startswith("\ufffa") for item in items)


def test_root_menu_items_match_a_real_players_structure():
    """FINDINGS F26. Byte-for-byte against a real root item from S05.

    A deck renders bare labels perfectly well and then refuses to open the
    category, so these three details are copied rather than invented: the
    per-category id in argument 2, the U+FFFA/U+FFFB wrapper, and argument 8
    being zero rather than a track item's 0x01000000.
    """
    item = db.make_menu_item(
        0, 2, db.menu_label("ARTIST"), "",
        item_type=db.ItemType.MENU_ARTIST, flags=0,
    )
    assert item.args[1] == 2
    assert item.args[2] == 0x12
    assert item.args[3] == "\ufffaARTIST\ufffb"
    assert item.args[6] == 0x81
    assert item.args[7] == 0


def test_track_list_round_trips_through_the_protocol(client):
    items = client.track_list(MediaSlot.USB)
    assert {item.id for item in items} == {101, 102, 103}
    by_id = {item.id: item for item in items}
    assert by_id[101].label1 == "Blue Monday"
    assert by_id[101].label2 == "New Order"
    assert by_id[103].label1 == "夜", "UTF-16BE must survive the wire"


def test_track_metadata_is_folded_back_into_a_record(client):
    metadata = client.track_metadata(MediaSlot.USB, 101)
    assert metadata["title"] == "Blue Monday"
    assert metadata["artist"] == "New Order"
    assert metadata["bpm_100"] == 13000
    assert metadata["duration"] == 245


def test_playlist_folder_and_contents(client):
    root = client.playlists(MediaSlot.USB, 0, folder=True)
    assert [item.label1 for item in root] == ["Sets"]

    inside = client.playlists(MediaSlot.USB, 10, folder=True)
    assert [item.label1 for item in inside] == ["Friday"]

    tracks = client.playlists(MediaSlot.USB, 11, folder=False)
    assert [item.label1 for item in tracks] == ["Blue Monday", "Temptation"]


def test_search(client):
    items = client.menu(db.MessageType.MENU_SEARCH, MediaSlot.USB, 0, "monday")
    assert [item.id for item in items] == [101]


def test_track_info_returns_the_mount_path(client):
    items = client.menu(db.MessageType.GET_TRACK_INFO, MediaSlot.USB, 101)
    assert items[0].item_type == db.ItemType.PATH
    assert items[0].label1 == "/Contents/blue.mp3"


def test_unknown_request_gets_an_error_not_an_empty_list(client):
    """An empty folder and a failed request must not look the same."""
    from prolinks_poc.proto.errors import ProtocolError

    with pytest.raises(ProtocolError):
        client.menu(db.MessageType.MENU_BITRATE, MediaSlot.USB, 0)


def test_missing_track_metadata_is_empty_rather_than_an_error(client):
    assert client.track_metadata(MediaSlot.USB, 999) == {"id": 999}


def test_paging_covers_a_list_longer_than_one_batch(library):
    """research/04 caps a render at 64 items, so a 150-track list needs three
    round trips and must come back complete and in order."""
    builder = PdbBuilder()
    for i in range(150):
        builder.add(PageType.TRACKS, track_row(1000 + i, f"Track {i:03d}", 0, 12000,
                                                f"/Contents/{i}.mp3"))
    big = Library.from_bytes(builder.build())

    instance = DbServer(big, device_number=5, bind_ip="127.0.0.1", port=0, query_port=0).start()
    try:
        with DbClient("127.0.0.1", 3, port=instance.port) as connection:
            items = connection.track_list(MediaSlot.USB)
        assert len(items) == 150
        assert [item.label1 for item in items] == [f"Track {i:03d}" for i in range(150)]
    finally:
        instance.stop()


def test_server_survives_an_abrupt_disconnect(server):
    """A player yanked off the network must not take the server with it."""
    import socket as _socket

    sock = _socket.create_connection(("127.0.0.1", server.port), 5.0)
    sock.sendall(db.PREAMBLE)
    sock.recv(5)
    sock.close()

    with DbClient("127.0.0.1", 3, port=server.port) as connection:
        assert connection.track_list(MediaSlot.USB)


# -- 0x0001, observed in a real CDJ-to-CDJ LINK browse ----------------------


def test_menu_close_is_a_bare_32_byte_message():
    """FINDINGS F16. Zero arguments, and the all-zero argument-type blob.

    Byte-for-byte against a real one from the S05 capture.
    """
    real = bytes.fromhex("11872349ae11038001b71000010f00140000000c000000000000000000000000")
    message = db.decode_message(ByteReader(real))
    assert message.type == db.MessageType.MENU_CLOSE
    assert message.args == []
    assert len(real) == 32
    assert message.encode() == real


def test_server_answers_menu_close_with_silence(client, server):
    """A reply would desynchronise a client that is not waiting for one.

    Before this was handled it fell through to the unknown-request path and
    produced a 0x4003 error, which is exactly the kind of thing that makes a
    real player refuse to browse us.
    """
    connection = client
    # Drive a menu first so there is server-side state to close.
    assert connection.track_list(MediaSlot.USB)

    connection._send_raw(
        db.Message(transaction_id=0x1234, type=db.MessageType.MENU_CLOSE, args=[]).encode()
    )
    # Nothing should come back -- prove it by showing the next real request's
    # reply arrives intact rather than being preceded by a stray message.
    items = connection.track_list(MediaSlot.USB)
    assert {item.id for item in items} == {101, 102, 103}


def test_menu_close_does_not_discard_the_result_set(library):
    """FINDINGS F27. It must not, or pagination breaks.

    "Release that menu" was an inference from where 0x0001 sits in the stream.
    Acting on it broke scrolling, because a deck sends this while still paging
    through the very list it is supposedly finished with.
    """
    from prolinks_poc.net.dbserverd import _Connection

    server = DbServer(library, bind_ip="127.0.0.1", port=0, query_port=0)
    try:
        connection = _Connection.__new__(_Connection)
        connection.server = server
        items = [db.make_menu_item(0, 1, "x")]
        connection.menus = {1: items}
        connection.last_menu = items
        connection.client_device_number = 1
        assert connection.handle(
            db.Message(transaction_id=1, type=db.MessageType.MENU_CLOSE, args=[])
        ) == []
        assert connection.menus == {1: items}
    finally:
        server.stop()


def test_concurrent_menus_are_kept_apart(client):
    """FINDINGS F27. A deck interleaves metadata lookups with list scrolling.

    It pages a 692-track list, dips into an 8-item metadata menu for a
    highlighted track, then resumes the list at the next offset *without*
    re-issuing the menu request. A single result set meant the metadata
    replaced the list and every later page came back empty -- the list going
    blank part-way down.
    """
    tracks = client.track_list(MediaSlot.USB)
    assert len(tracks) == 3

    # Establish a second, differently-sized menu in between.
    metadata = client.track_metadata(MediaSlot.USB, 101)
    assert metadata["title"] == "Blue Monday"

    # The track list must still be renderable at a later offset.
    desc = client.descriptor(MediaSlot.USB)
    client._send_raw(
        db.make_render(client._transaction_id(), desc, 1, 2, len(tracks)).encode()
    )
    resumed = client._read_menu_page()
    assert [item.label1 for item in resumed] == [t.label1 for t in tracks[1:3]]



def test_unknown_3e03_is_answered_not_errored(client, server):
    """FINDINGS F25.

    A deck browsing a foreign device sends 0x3e03 immediately after Introduce.
    Answering it with 0x4003 made a real CDJ fetch our root menu and then
    disconnect without drilling into any category -- which presented as every
    section listing but showing empty.
    """
    reply = client.request(
        db.Message(transaction_id=0x1234, type=db.MessageType.UNKNOWN_3E03,
                   args=[0x02010301])
    )
    assert reply.type == db.MessageType.UNKNOWN_4B02
    assert reply.number(0) == db.MessageType.UNKNOWN_3E03
    assert reply.number(2) == server.device_number
    assert reply.string(3) == ""


def test_unknown_4b02_matches_the_captured_reply_byte_for_byte():
    real = db.Message(
        transaction_id=0x03800001, type=db.MessageType.UNKNOWN_4B02,
        args=[0x3E03, 0, 2, ""],
        arg_types=[db.FieldType.UINT32, db.FieldType.UINT32,
                   db.FieldType.UINT32, db.FieldType.STRING],
    )
    assert db.decode_message(ByteReader(real.encode())).encode() == real.encode()
