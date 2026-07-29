"""Serving two media at once — the TriMiXxX shape.

F37 settled how a player sees two media on one peer, and it is not the obvious
arrangement: **one** dbserver connection carries both, distinguished only by the
slot byte in each request's descriptor. So the tests here are mostly about that
one connection returning the right library per message, and about the two media
not colliding in the filehandle space.
"""

from __future__ import annotations

import pytest

from prolinks_poc.core.library import Library
from prolinks_poc.core.medium import Medium
from prolinks_poc.core.slots import MediaSlot
from prolinks_poc.net.dbserverd import DbServer, _Connection
from prolinks_poc.net.loop import EventLoop
from prolinks_poc.net.nfsclient import NfsClient
from prolinks_poc.net.nfsserver import NfsServer
from prolinks_poc.net.vfs import Vfs
from prolinks_poc.proto import dbserver as db

from pdb_fixtures import PdbBuilder, artist_row, sample_library_bytes, track_row
from prolinks_poc.proto.pdb import PageType


def library_with(title: str, track_id: int) -> Library:
    builder = PdbBuilder()
    builder.add(PageType.ARTISTS, artist_row(1, "A"))
    builder.add(PageType.TRACKS,
                track_row(track_id, title, 1, 12800, f"/Contents/{title}.mp3"))
    return Library.from_bytes(builder.build())


@pytest.fixture
def two_media():
    return {
        int(MediaSlot.USB): Medium(
            slot=MediaSlot.USB, library=library_with("OnUsb", 101),
            volume_name="USBSTICK",
        ),
        int(MediaSlot.SD): Medium(
            slot=MediaSlot.SD, library=library_with("OnSd", 202),
            volume_name="SDCARD", settings=b"\x81" * 32,
        ),
    }


def connection_for(server) -> _Connection:
    connection = _Connection.__new__(_Connection)
    connection.server = server
    connection.peer = ("127.0.0.1", 1)
    connection.menus = {}
    connection.last_menu = []
    connection.client_device_number = 2
    return connection


def test_one_connection_serves_both_libraries(two_media):
    """The point of F37: the slot comes from the message, not the connection.

    A DJ switching slots mid-browse reuses the same TCP connection, so caching
    the medium per connection would quietly serve the wrong library.
    """
    server = DbServer(two_media, bind_ip="127.0.0.1", port=0, query_port=0)
    connection = connection_for(server)

    def titles(slot):
        descriptor = db.descriptor(2, slot)
        replies = connection.handle(
            db.Message(1, db.MessageType.MENU_TRACK, [descriptor, 0])
        )
        assert replies[0].type is db.MessageType.SUCCESS
        return [
            item.args[3]
            for item in connection.menus[replies[0].args[1]]
        ]

    assert titles(MediaSlot.USB) == ["OnUsb"]
    assert titles(MediaSlot.SD) == ["OnSd"]
    # ...and interleaved, which is what a real deck does.
    assert titles(MediaSlot.USB) == ["OnUsb"]


def test_an_unknown_slot_falls_back_to_the_only_medium():
    """A single-slot server must keep answering requests naming any slot --
    that is what it did before two slots existed."""
    server = DbServer(Library.from_bytes(sample_library_bytes()),
                      bind_ip="127.0.0.1", port=0, query_port=0)
    for slot in (MediaSlot.USB, MediaSlot.SD, MediaSlot.REKORDBOX):
        assert server.medium_for(db.descriptor(2, slot)) is server.default_medium


def test_medium_for_reads_the_descriptors_slot_byte(two_media):
    server = DbServer(two_media, bind_ip="127.0.0.1", port=0, query_port=0)
    assert server.medium_for(db.descriptor(2, MediaSlot.USB)).volume_name == "USBSTICK"
    assert server.medium_for(db.descriptor(2, MediaSlot.SD)).volume_name == "SDCARD"


def test_analysis_and_artwork_come_from_the_right_medium(two_media, tmp_path):
    """Each medium reads its own files; a shared root would cross them over."""
    usb, sd = tmp_path / "usb", tmp_path / "sd"
    for root in (usb, sd):
        (root / "PIONEER").mkdir(parents=True)
    two_media[int(MediaSlot.USB)].root = usb
    two_media[int(MediaSlot.SD)].root = sd

    usb_medium = two_media[int(MediaSlot.USB)]
    usb_medium.library.artwork[7] = "/PIONEER/cover.jpg"
    (usb / "PIONEER" / "cover.jpg").write_bytes(b"USBIMAGE")

    sd_medium = two_media[int(MediaSlot.SD)]
    sd_medium.library.artwork[7] = "/PIONEER/cover.jpg"
    (sd / "PIONEER" / "cover.jpg").write_bytes(b"SDIMAGE")

    assert usb_medium.artwork_for(7) == b"USBIMAGE"
    assert sd_medium.artwork_for(7) == b"SDIMAGE"


# -- the NFS half ---------------------------------------------------------


@pytest.fixture
def nfs_stack():
    """One VFS, two media under their own subtrees, exported as /C/ and /B/."""
    loop = EventLoop()
    vfs = Vfs.from_mapping({
        "C/Contents/usb.mp3": b"usb audio",
        "C/PIONEER/rekordbox/export.pdb": b"usb pdb",
        "B/Contents/sd.mp3": b"sd audio",
        "B/PIONEER/rekordbox/export.pdb": b"sd pdb",
    })
    server = NfsServer(loop, vfs, exports={"/C/": "/C", "/B/": "/B"})
    server.start()
    client = NfsClient(loop, "127.0.0.1", local_ip="127.0.0.1",
                       portmap_port=server.portmap_port, timeout=1.0, retries=2)
    try:
        yield client, server, vfs
    finally:
        client.close()
        server.close()
        loop.close()


def test_mounting_each_export_yields_a_different_handle(nfs_stack):
    """The collision this design exists to avoid.

    Both media would otherwise hash the same relative paths to the same
    filehandles -- most obviously the root -- and a CDJ keeps only the leading
    12 bytes (F28), so there would be nothing left to disambiguate them.
    """
    client, _server, _vfs = nfs_stack
    usb = client.mount("/C/")
    sd = client.mount("/B/")
    assert usb != sd
    assert usb[:12] != sd[:12], "must differ within the bytes a CDJ preserves"


def test_each_export_sees_only_its_own_medium(nfs_stack):
    client, _server, _vfs = nfs_stack
    usb = client.mount("/C/")
    sd = client.mount("/B/")

    usb_entries = {e.name for e in client.readdir(usb)[0]}
    sd_entries = {e.name for e in client.readdir(sd)[0]}
    assert usb_entries == sd_entries == {"Contents", "PIONEER"}

    handle, attrs = client.lookup_path(usb, "Contents/usb.mp3")
    assert client.download(handle, attrs.size) == b"usb audio"
    handle, attrs = client.lookup_path(sd, "Contents/sd.mp3")
    assert client.download(handle, attrs.size) == b"sd audio"

    # The other medium's file is genuinely absent from this export.
    with pytest.raises(Exception):
        client.lookup_path(usb, "Contents/sd.mp3")


def test_both_exports_are_listed(nfs_stack):
    client, _server, _vfs = nfs_stack
    assert {e.path for e in client.list_exports()} == {"/C/", "/B/"}


# -- announcing two slots -------------------------------------------------


def announcer_with(media):
    """A VirtualCdj wired just enough to answer queries, with a fake channel."""
    from prolinks_poc.core.announcer import VirtualCdj

    virtual = VirtualCdj.__new__(VirtualCdj)
    virtual.device_number = 3
    virtual.name = "CDJ-2000nexus"
    virtual.media = dict(media)
    virtual.media_name = ""
    virtual.track_count = 0
    virtual.playlist_count = 0
    virtual.device_settings = b""
    virtual.has_usb = False
    virtual.discovery = type("D", (), {"table": {}})()
    virtual.media_queries_answered = 0
    virtual.settings_queries_answered = 0
    virtual._status_counter = 0
    virtual.sent = []

    class Channel:
        def sendto(self, data, peer):
            virtual.sent.append((data, peer))

    virtual._query_channel = Channel()
    virtual._status_channel = Channel()
    return virtual


def query_for(slot, target=3, requester=2):
    from prolinks_poc.proto import djl_status as status

    packet = bytearray(0x30)
    packet[:10] = b"Qspt1WmJOL"
    packet[10] = status.StatusType.MEDIA_QUERY
    packet[0x0B:0x1F] = b"CDJ-2000nexus".ljust(20, b"\x00")
    packet[0x1F] = 0x01
    packet[0x21] = requester
    packet[0x24:0x28] = (requester).to_bytes(4, "big")
    packet[0x28:0x2C] = (target).to_bytes(4, "big")
    packet[0x2C:0x30] = int(slot).to_bytes(4, "big")
    return bytes(packet)


def test_media_query_is_answered_per_slot(two_media):
    """Each slot reports its own volume name and counts, from one device."""
    from prolinks_poc.proto import djl_status as status

    virtual = announcer_with(two_media)
    for slot, expected in ((MediaSlot.USB, "USBSTICK"), (MediaSlot.SD, "SDCARD")):
        virtual.sent.clear()
        virtual._on_status_datagram(query_for(slot), ("127.0.0.1", 50002))
        assert len(virtual.sent) == 1, f"slot {slot.name} went unanswered"
        reply = virtual.sent[0][0]
        assert reply[status.OFF_MR_SLOT + 3] == int(slot)
        name = reply[status.OFF_MR_NAME:status.OFF_MR_NAME + status.LEN_MR_NAME]
        assert name.decode("utf-16-be").rstrip("\x00") == expected


def test_a_query_for_an_unserved_slot_is_ignored(two_media):
    """Answering would tell the deck the slot exists and holds nothing, and it
    would then offer an empty medium (F24). Silence is the honest answer."""
    virtual = announcer_with(two_media)
    virtual._on_status_datagram(query_for(MediaSlot.CD), ("127.0.0.1", 50002))
    assert virtual.sent == []


def test_status_packet_advertises_exactly_the_slots_we_serve(two_media):
    from prolinks_poc.proto import djl_status as status

    both = status.decode_status(announcer_with(two_media).build_status())
    assert both.has_usb and both.has_sd

    usb_only = {int(MediaSlot.USB): two_media[int(MediaSlot.USB)]}
    one = status.decode_status(announcer_with(usb_only).build_status())
    assert one.has_usb and not one.has_sd


def test_settings_are_answered_from_the_slot_that_was_asked_about(two_media):
    """FINDINGS F38 per slot: the SD medium has settings, the USB one does not."""
    from prolinks_poc.proto import djl_status as status

    virtual = announcer_with(two_media)
    for slot, expected in ((MediaSlot.SD, b"\x81" * 32), (MediaSlot.USB, bytes(32))):
        virtual.sent.clear()
        packet = bytearray(0x28)
        packet[:10] = b"Qspt1WmJOL"
        packet[10] = status.StatusType.SETTINGS_QUERY
        packet[0x0B:0x1F] = b"CDJ-2000nexus".ljust(20, b"\x00")
        packet[0x1F] = 0x01
        packet[0x21] = 2
        packet[0x24] = 2
        packet[0x25] = int(slot)
        virtual._answer_settings_query(bytes(packet), ("127.0.0.1", 50002))
        assert len(virtual.sent) == 1
        assert virtual.sent[0][0][status.OFF_SET_PAYLOAD:] == expected
