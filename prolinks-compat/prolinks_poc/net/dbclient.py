"""dbserver client: browse another player's library over TCP.

Unlike the UDP side, this protocol is strictly request/response over a
long-lived TCP connection, so it is written with blocking sockets and a
timeout rather than folded into the reactor. That is also the shape the Qt port
wants -- ``QTcpSocket`` with ``waitForReadyRead`` maps onto it directly, and a
state machine would buy nothing here.

**The device-number constraint is the awkward part.** Every request carries our
player number in the top byte of its descriptor, and the server validates it:
it must be 1-4, must belong to a device actually on the network, and must not
be the player we are talking to (``research/04`` §2.3). So the safe observer
number 7 that the announcer defaults to *cannot* be used for dbserver queries.
That is the whole reason the NFS path is worth having: it needs no number at
all.
"""

from __future__ import annotations

import logging
import socket

from ..core.slots import MediaSlot
from ..proto import dbserver as db
from ..proto.bytes import ByteReader
from ..proto.errors import DecodeError, ProtocolError

log = logging.getLogger(__name__)

__all__ = ["DbClient", "DbServerUnavailable", "MenuItem", "discover_port"]

DEFAULT_TIMEOUT_S = 5.0


class DbServerUnavailable(ProtocolError):
    """The peer is not running a reachable dbserver."""


def discover_port(peer_ip: str, timeout: float = DEFAULT_TIMEOUT_S) -> int:
    """Ask port 12523 where the dbserver is listening.

    Always 1051 in every capture, but it is documented as dynamic and the query
    costs one short-lived connection, so ask rather than assume.
    """
    try:
        with socket.create_connection((peer_ip, db.QUERY_PORT), timeout) as sock:
            sock.sendall(db.PORT_QUERY_PACKET)
            reply = sock.recv(2)
    except OSError as exc:
        raise DbServerUnavailable(
            f"{peer_ip}:{db.QUERY_PORT} did not answer the port query: {exc}"
        ) from exc
    if len(reply) != 2:
        raise DbServerUnavailable(
            f"{peer_ip}:{db.QUERY_PORT} returned {len(reply)} bytes, expected 2"
        )
    return int.from_bytes(reply, "big")


class MenuItem:
    """One ``0x4101`` row, with its twelve positional arguments named."""

    __slots__ = ("parent_id", "id", "label1", "label2", "item_type", "artwork_id",
                 "playlist_position", "raw")

    def __init__(self, message: db.Message) -> None:
        self.raw = message
        self.parent_id = message.number(0)
        self.id = message.number(1)
        self.label1 = message.string(3)
        self.label2 = message.string(5)
        # CDJ-3000s pack extra data into the high bytes of the type.
        self.item_type = db.item_type_of(message.number(6))
        self.artwork_id = message.number(8)
        self.playlist_position = message.number(9)

    @property
    def type_name(self) -> str:
        try:
            return db.ItemType(self.item_type).name
        except ValueError:
            return f"0x{self.item_type:04x}"

    def __str__(self) -> str:
        second = f"  / {self.label2}" if self.label2 else ""
        return f"{self.id:>8}  {self.type_name:<16} {self.label1}{second}"


class DbClient:
    """A connection to one player's dbserver."""

    def __init__(
        self,
        peer_ip: str,
        device_number: int,
        port: int | None = None,
        timeout: float = DEFAULT_TIMEOUT_S,
        recorder=None,
    ) -> None:
        self.peer_ip = peer_ip
        self.device_number = device_number
        self.timeout = timeout
        self.recorder = recorder
        self.port = port
        self.server_device_number: int | None = None

        self._sock: socket.socket | None = None
        self._buffer = b""
        # Real players start well above 1 (0x03800001 was observed). The value
        # is opaque and only has to be unique per connection, but starting in
        # the same region is one less way to look unusual.
        self._next_transaction = 0x03800001

    # -- connection ------------------------------------------------------

    def connect(self) -> "DbClient":
        if self.port is None:
            self.port = discover_port(self.peer_ip, self.timeout)
            log.info("%s dbserver is on port %d", self.peer_ip, self.port)

        try:
            self._sock = socket.create_connection((self.peer_ip, self.port), self.timeout)
        except OSError as exc:
            raise DbServerUnavailable(
                f"could not connect to {self.peer_ip}:{self.port}: {exc}"
            ) from exc
        self._sock.settimeout(self.timeout)

        # The preamble: a bare UInt32 field with value 1, echoed back.
        self._send_raw(db.PREAMBLE)
        echoed = self._recv_exact(len(db.PREAMBLE))
        if echoed != db.PREAMBLE:
            raise ProtocolError(
                f"{self.peer_ip} answered the preamble with {echoed.hex()}, "
                f"expected {db.PREAMBLE.hex()}"
            )

        reply = self.request(db.make_introduce(self.device_number))
        # The Introduce reply's second argument is the *server's* player
        # number, not an item count as it is for every other 0x4000.
        self.server_device_number = reply.number(1)
        log.info(
            "connected to %s as device %d; peer reports device %d",
            self.peer_ip, self.device_number, self.server_device_number,
        )
        return self

    def close(self) -> None:
        if self._sock is not None:
            try:
                self._send_raw(db.make_disconnect().encode())
            except OSError:
                pass
            self._sock.close()
            self._sock = None

    def __enter__(self) -> "DbClient":
        return self.connect()

    def __exit__(self, *exc) -> None:
        self.close()

    # -- transport -------------------------------------------------------

    def _send_raw(self, data: bytes) -> None:
        if self._sock is None:
            raise ProtocolError("not connected")
        self._sock.sendall(data)
        if self.recorder is not None:
            self.recorder.record("tx", self.port or 0, (self.peer_ip, self.port or 0), data)

    def _recv_exact(self, count: int) -> bytes:
        while len(self._buffer) < count:
            chunk = self._sock.recv(65536)
            if not chunk:
                raise ProtocolError(f"{self.peer_ip} closed the connection")
            if self.recorder is not None:
                self.recorder.record(
                    "rx", self.port or 0, (self.peer_ip, self.port or 0), chunk
                )
            self._buffer += chunk
        out, self._buffer = self._buffer[:count], self._buffer[count:]
        return out

    def _recv_message(self) -> db.Message:
        """Read until one whole message is available.

        dbserver messages are not length-framed, so "is it complete?" can only
        be answered by trying to parse: a truncated message raises, which means
        wait for more bytes rather than fail.
        """
        while True:
            if self._buffer:
                reader = ByteReader(self._buffer)
                try:
                    message = db.decode_message(reader)
                except DecodeError:
                    pass
                else:
                    self._buffer = self._buffer[reader.pos :]
                    return message
            chunk = self._sock.recv(65536)
            if not chunk:
                raise ProtocolError(f"{self.peer_ip} closed the connection")
            if self.recorder is not None:
                self.recorder.record(
                    "rx", self.port or 0, (self.peer_ip, self.port or 0), chunk
                )
            self._buffer += chunk

    def _transaction_id(self) -> int:
        value = self._next_transaction
        self._next_transaction += 1
        return value

    def request(self, message: db.Message) -> db.Message:
        """Send one message and read its reply."""
        self._send_raw(message.encode())
        reply = self._recv_message()
        if reply.type == db.MessageType.ERROR:
            raise ProtocolError(f"{message.type_name} rejected by {self.peer_ip}")
        return reply

    # -- queries ---------------------------------------------------------

    def descriptor(
        self, slot: MediaSlot, menu: int = db.MenuTarget.MAIN,
        track_type: int = db.TrackType.REKORDBOX,
    ) -> int:
        return db.descriptor(self.device_number, int(slot), menu, track_type)

    def menu(
        self,
        request_type: int,
        slot: MediaSlot,
        *extra_args: int,
        menu_target: int = db.MenuTarget.MAIN,
        batch: int = db.MAX_RENDER_BATCH,
    ) -> list[MenuItem]:
        """Run a menu request and page through every item it produced.

        Two round trips minimum: the request itself answers with a count, then
        ``0x3000`` renders a window of that many items. Large lists must be
        paged -- ``research/04`` §4.3 documents 64 as safe and thousands as
        failing outright.
        """
        desc = self.descriptor(slot, menu_target)
        transaction = self._transaction_id()
        reply = self.request(
            db.make_menu_request(transaction, request_type, desc, *extra_args)
        )
        if reply.type != db.MessageType.SUCCESS:
            raise ProtocolError(
                f"expected SUCCESS for {db.MessageType(request_type).name}, "
                f"got {reply.type_name}"
            )
        count = reply.number(1)
        if count == 0xFFFFFFFF:
            # The documented "not found" sentinel, not an error condition.
            return []

        items: list[MenuItem] = []
        offset = 0
        while offset < count:
            limit = min(batch, count - offset)
            self._send_raw(
                db.make_render(self._transaction_id(), desc, offset, limit, limit).encode()
            )
            items.extend(self._read_menu_page())
            offset += limit
        return items

    def _read_menu_page(self) -> list[MenuItem]:
        """Consume one header / items… / footer sequence."""
        items: list[MenuItem] = []
        seen_header = False
        while True:
            message = self._recv_message()
            if message.type == db.MessageType.MENU_HEADER:
                seen_header = True
            elif message.type == db.MessageType.MENU_ITEM:
                items.append(MenuItem(message))
            elif message.type == db.MessageType.MENU_FOOTER:
                return items
            elif message.type == db.MessageType.ERROR:
                raise ProtocolError("player rejected the render request")
            elif seen_header:
                log.debug("unexpected %s inside a menu page", message.type_name)
        return items

    def track_metadata(self, slot: MediaSlot, track_id: int) -> dict:
        """Full metadata for one track, as a flat dict.

        The reply is a *menu* whose items each carry one field, discriminated
        by item type -- title, artist, album, duration and so on -- so it has
        to be folded back into a record.
        """
        items = self.menu(db.MessageType.GET_METADATA, slot, track_id)
        metadata: dict = {"id": track_id}
        for item in items:
            kind = item.item_type
            if kind == db.ItemType.TRACK_TITLE:
                metadata["title"] = item.label1
                metadata["artwork_id"] = item.artwork_id
                metadata["artist_id"] = item.parent_id
            elif kind == db.ItemType.ARTIST:
                metadata["artist"] = item.label1
            elif kind == db.ItemType.ALBUM:
                metadata["album"] = item.label1
            elif kind == db.ItemType.GENRE:
                metadata["genre"] = item.label1
            elif kind == db.ItemType.KEY:
                metadata["key"] = item.label1
            elif kind == db.ItemType.COMMENT:
                metadata["comment"] = item.label1
            elif kind == db.ItemType.DATE_ADDED:
                metadata["date_added"] = item.label1
            elif kind == db.ItemType.DURATION:
                metadata["duration"] = item.id
            elif kind == db.ItemType.TEMPO:
                # Stored ×100, like everywhere else in this protocol family.
                metadata["bpm_100"] = item.id
            elif kind == db.ItemType.RATING:
                metadata["rating"] = item.id
        return metadata

    def track_list(
        self, slot: MediaSlot, sort: int = db.SortOrder.DEFAULT
    ) -> list[MenuItem]:
        """Every track in a slot. The sort also picks the second column."""
        return self.menu(db.MessageType.MENU_TRACK, slot, sort)

    def root_menu(self, slot: MediaSlot) -> list[MenuItem]:
        """Which browse categories this medium exposes."""
        return self.menu(db.MessageType.MENU_ROOT, slot, 0, 0x00FFFFFF)

    def playlists(
        self, slot: MediaSlot, playlist_id: int = 0, folder: bool = True,
        sort: int = db.SortOrder.DEFAULT,
    ) -> list[MenuItem]:
        """List a playlist folder, or the tracks of one playlist.

        ``playlist_id=0, folder=True`` is the root of the tree.
        """
        return self.menu(
            db.MessageType.MENU_PLAYLIST, slot, sort, playlist_id, 1 if folder else 0
        )

    def artwork(self, slot: MediaSlot, artwork_id: int) -> bytes:
        """Fetch one album-art image.

        Binary loads use ``M=08`` in the descriptor, and the blob argument is
        omitted entirely when empty -- so a track with no art yields ``b""``
        rather than an error.
        """
        desc = self.descriptor(slot, db.MenuTarget.BINARY)
        reply = self.request(
            db.make_menu_request(
                self._transaction_id(), db.MessageType.GET_ARTWORK, desc, artwork_id
            )
        )
        return reply.blob(3) or reply.blob(2)
