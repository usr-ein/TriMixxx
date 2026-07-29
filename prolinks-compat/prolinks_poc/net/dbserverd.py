"""dbserver *server*: let real CDJs browse our library.

The half nobody has built. Grepping all seven reference projects for TCP
listeners turns up an OBS overlay web server and an HTTP API — nothing that
answers a dbserver query. So this is written from ``research/04`` alone, and
validated the only way available before hardware: our own client drives it, and
the messages it emits are the same shapes the codec round-trips byte-exactly
against real captures.

Structure mirrors what a real player does:

* a listener on **12523** answering the fixed port query with our dbserver port;
* a listener on the dbserver port, one thread per connection, holding
  per-connection state — because the protocol *is* stateful: a menu request
  establishes a result set, and the ``0x3000`` render that follows pages
  through it.

Threads rather than the reactor, deliberately. dbserver connections are
long-lived, blocking and independent; a real player opens one, browses, and
disconnects. In the Qt port each becomes a ``QTcpSocket`` on the network
thread, which is closer to a thread-per-connection shape than to the UDP
event loop.
"""

from __future__ import annotations

import logging
import socket
import threading

from ..core.library import Library
from ..core.slots import MediaSlot
from ..proto import dbserver as db
from ..proto.bytes import ByteReader
from ..proto.errors import DecodeError

log = logging.getLogger(__name__)

__all__ = ["DbServer"]


class _Connection(threading.Thread):
    """One client conversation. Owns the menu result set being paged through."""

    def __init__(self, server: "DbServer", sock: socket.socket, peer) -> None:
        super().__init__(daemon=True, name=f"dbserver-{peer[0]}")
        self.server = server
        self.sock = sock
        self.peer = peer
        self.buffer = b""
        #: The result set established by the last menu request, waiting for
        #: its render. Keyed by nothing: a client runs one menu at a time.
        self.pending: list[db.Message] = []
        self.client_device_number = 0

    # -- framing ---------------------------------------------------------

    def _send(self, data: bytes) -> None:
        self.sock.sendall(data)
        if self.server.recorder is not None:
            self.server.recorder.record("tx", self.server.port, self.peer, data)

    def _recv_message(self) -> db.Message | None:
        while True:
            if self.buffer:
                reader = ByteReader(self.buffer)
                try:
                    message = db.decode_message(reader)
                except DecodeError:
                    pass
                else:
                    self.buffer = self.buffer[reader.pos :]
                    return message
            chunk = self.sock.recv(65536)
            if not chunk:
                return None
            if self.server.recorder is not None:
                self.server.recorder.record("rx", self.server.port, self.peer, chunk)
            self.buffer += chunk

    def run(self) -> None:
        try:
            self._serve()
        except (OSError, DecodeError) as exc:
            log.debug("connection from %s ended: %s", self.peer[0], exc)
        finally:
            self.sock.close()
            log.info("dbserver client %s disconnected", self.peer[0])

    def _serve(self) -> None:
        # The preamble is echoed verbatim before any message is exchanged.
        preamble = b""
        while len(preamble) < len(db.PREAMBLE):
            chunk = self.sock.recv(len(db.PREAMBLE) - len(preamble))
            if not chunk:
                return
            preamble += chunk
        self._send(db.PREAMBLE)

        while True:
            message = self._recv_message()
            if message is None:
                return
            self.server.stats[message.type_name] = (
                self.server.stats.get(message.type_name, 0) + 1
            )
            for reply in self.handle(message):
                self._send(reply.encode())

    # -- dispatch --------------------------------------------------------

    def handle(self, message: db.Message) -> list[db.Message]:
        transaction = message.transaction_id

        if message.type == db.MessageType.INTRODUCE:
            self.client_device_number = message.number(0)
            log.info("client %s introduced itself as device %d",
                     self.peer[0], self.client_device_number)
            # Argument 2 is our own player number here, not an item count.
            return [db.Message(transaction, db.MessageType.SUCCESS,
                               [message.type, self.server.device_number])]

        if message.type == db.MessageType.DISCONNECT:
            raise OSError("client asked to disconnect")

        if message.type == db.MessageType.RENDER_MENU:
            return self._render(message)

        items = self.server.build_menu(message)
        if items is None:
            log.info("unsupported request %s from %s", message.type_name, self.peer[0])
            return [db.Message(transaction, db.MessageType.ERROR, [message.type, 0])]

        # Establish the result set, then answer with its size. The client
        # follows up with 0x3000 to page through it.
        self.pending = items
        return [db.Message(transaction, db.MessageType.SUCCESS,
                           [message.type, len(items)])]

    def _render(self, message: db.Message) -> list[db.Message]:
        offset = message.number(1)
        limit = message.number(2)
        window = self.pending[offset : offset + limit]

        out = [db.Message(message.transaction_id, db.MessageType.MENU_HEADER, [1, 0])]
        for item in window:
            # Items are built without a transaction id; stamp them with the
            # render's so the client can correlate the whole page.
            item.transaction_id = message.transaction_id
            out.append(item)
        out.append(db.Message(message.transaction_id, db.MessageType.MENU_FOOTER, []))
        return out


class DbServer:
    """Serves one :class:`Library` per media slot to real players."""

    def __init__(
        self,
        library: Library,
        device_number: int = 5,
        slot: MediaSlot = MediaSlot.USB,
        bind_ip: str = "0.0.0.0",
        port: int = 0,
        query_port: int = db.QUERY_PORT,
        recorder=None,
    ) -> None:
        self.library = library
        self.device_number = device_number
        self.slot = slot
        self.recorder = recorder
        self.stats: dict[str, int] = {}

        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._listener.bind((bind_ip, port))
        self._listener.listen(8)
        self.port = self._listener.getsockname()[1]

        # Port 12523 is fixed and below 1024 only on... no, it is not
        # privileged, but it may already be held by rekordbox on the same host.
        self._query_listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._query_listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._query_listener.bind((bind_ip, query_port))
        self._query_listener.listen(8)
        self.query_port = self._query_listener.getsockname()[1]

        self._threads: list[threading.Thread] = []
        self._running = False

    # -- lifecycle -------------------------------------------------------

    def start(self) -> "DbServer":
        self._running = True
        for listener, handler in (
            (self._query_listener, self._serve_port_query),
            (self._listener, self._serve_dbserver),
        ):
            thread = threading.Thread(target=self._accept_loop, args=(listener, handler),
                                      daemon=True)
            thread.start()
            self._threads.append(thread)
        log.info(
            "dbserver listening on %d (port query on %d) as device %d",
            self.port, self.query_port, self.device_number,
        )
        return self

    def stop(self) -> None:
        self._running = False
        for listener in (self._listener, self._query_listener):
            try:
                listener.close()
            except OSError:
                pass

    def __enter__(self) -> "DbServer":
        return self.start()

    def __exit__(self, *exc) -> None:
        self.stop()

    def _accept_loop(self, listener: socket.socket, handler) -> None:
        while self._running:
            try:
                sock, peer = listener.accept()
            except OSError:
                return
            handler(sock, peer)

    def _serve_port_query(self, sock: socket.socket, peer) -> None:
        """Answer the fixed 19-byte query with our dbserver port."""
        try:
            request = sock.recv(64)
            if request.startswith(db.PORT_QUERY_PACKET[:4]):
                sock.sendall(self.port.to_bytes(2, "big"))
                log.info("told %s our dbserver is on port %d", peer[0], self.port)
        except OSError:
            pass
        finally:
            sock.close()

    def _serve_dbserver(self, sock: socket.socket, peer) -> None:
        log.info("dbserver client connected from %s", peer[0])
        connection = _Connection(self, sock, peer)
        connection.start()
        self._threads.append(connection)

    # -- menu construction -----------------------------------------------

    def build_menu(self, message: db.Message) -> list[db.Message] | None:
        """Turn a menu request into the items it should produce.

        Returns ``None`` for a request we do not implement, which becomes a
        ``0x4003`` error rather than a silent empty list — a player showing an
        empty folder when it should show a failure is worse than an error.
        """
        request_type = message.type

        if request_type == db.MessageType.MENU_ROOT:
            return self._root_menu()
        if request_type == db.MessageType.MENU_TRACK:
            return self._track_list(message.number(1))
        if request_type == db.MessageType.MENU_PLAYLIST:
            return self._playlist_menu(message.number(2), bool(message.number(3)))
        if request_type in (db.MessageType.GET_METADATA, db.MessageType.GET_GENERIC_METADATA):
            return self._metadata(message.number(1))
        if request_type == db.MessageType.MENU_ARTIST:
            return self._by_name(self.library.artists, db.ItemType.ARTIST)
        if request_type == db.MessageType.MENU_ALBUM:
            return self._by_name(self.library.albums, db.ItemType.ALBUM)
        if request_type == db.MessageType.MENU_GENRE:
            return self._by_name(self.library.genres, db.ItemType.GENRE)
        if request_type == db.MessageType.MENU_KEY:
            return self._by_name(self.library.keys, db.ItemType.KEY)
        if request_type == db.MessageType.GET_TRACK_INFO:
            return self._track_info(message.number(1))
        if request_type == db.MessageType.MENU_SEARCH:
            return self._search(message.string(2))
        return None

    def _root_menu(self) -> list[db.Message]:
        """Which browse categories we claim to offer.

        A real player renders exactly the entries we list here, so only
        advertise what :meth:`build_menu` can actually answer.
        """
        entries = [
            (db.ItemType.MENU_ARTIST, "ARTIST"),
            (db.ItemType.MENU_ALBUM, "ALBUM"),
            (db.ItemType.MENU_TRACK, "TRACK"),
            (db.ItemType.MENU_GENRE, "GENRE"),
            (db.ItemType.MENU_KEY, "KEY"),
            (db.ItemType.MENU_PLAYLIST, "PLAYLIST"),
        ]
        return [
            db.make_menu_item(0, 0, label, "", item_type=item_type)
            for item_type, label in entries
        ]

    def _track_list(self, sort: int) -> list[db.Message]:
        tracks = self.library.track_list()
        if sort == db.SortOrder.BPM:
            tracks = sorted(tracks, key=lambda t: t.bpm_100)
        elif sort == db.SortOrder.TITLE:
            tracks = sorted(tracks, key=lambda t: t.title.lower())
        return [
            db.make_menu_item(
                0, track.id, track.title, track.artist,
                item_type=db.ItemType.TITLE_AND_ARTIST,
                artwork_id=0,
            )
            for track in tracks
        ]

    def _playlist_menu(self, playlist_id: int, folder: bool) -> list[db.Message]:
        if folder:
            children = (
                self.library.root_playlists
                if playlist_id == 0
                else self.library.playlists[playlist_id].children
                if playlist_id in self.library.playlists
                else []
            )
            return [
                db.make_menu_item(
                    0, playlist.id, playlist.name, "",
                    item_type=db.ItemType.FOLDER if playlist.is_folder else db.ItemType.PLAYLIST,
                    playlist_position=position,
                )
                for position, playlist in enumerate(children, start=1)
            ]
        return [
            db.make_menu_item(
                0, track.id, track.title, track.artist,
                item_type=db.ItemType.TITLE_AND_ARTIST,
                playlist_position=position,
            )
            for position, track in enumerate(
                self.library.playlist_tracks(playlist_id), start=1
            )
        ]

    def _metadata(self, track_id: int) -> list[db.Message]:
        """One track's metadata, as the per-field menu the protocol expects."""
        track = self.library.tracks.get(track_id)
        if track is None:
            return []
        items = [
            db.make_menu_item(0, track.id, track.title, "",
                              item_type=db.ItemType.TRACK_TITLE),
            db.make_menu_item(0, track.id, track.artist, "",
                              item_type=db.ItemType.ARTIST),
            db.make_menu_item(0, track.id, track.album, "",
                              item_type=db.ItemType.ALBUM),
            # Numeric fields carry their value in the id, not the label.
            db.make_menu_item(0, track.duration, "", "", item_type=db.ItemType.DURATION),
            db.make_menu_item(0, track.bpm_100, "", "", item_type=db.ItemType.TEMPO),
            db.make_menu_item(0, track.rating, "", "", item_type=db.ItemType.RATING),
        ]
        if track.genre:
            items.append(db.make_menu_item(0, track.id, track.genre, "",
                                           item_type=db.ItemType.GENRE))
        if track.key:
            items.append(db.make_menu_item(0, track.id, track.key, "",
                                           item_type=db.ItemType.KEY))
        if track.comment:
            items.append(db.make_menu_item(0, track.id, track.comment, "",
                                           item_type=db.ItemType.COMMENT))
        return items

    def _track_info(self, track_id: int) -> list[db.Message]:
        """The mount path of a track, as a single ``PATH`` item."""
        track = self.library.tracks.get(track_id)
        if track is None:
            return []
        return [db.make_menu_item(0, track.id, track.path, "", item_type=db.ItemType.PATH)]

    def _search(self, term: str) -> list[db.Message]:
        return [
            db.make_menu_item(
                0, track.id, track.title, track.artist,
                item_type=db.ItemType.TITLE_AND_ARTIST,
            )
            for track in self.library.search(term)
        ]

    def _by_name(self, mapping: dict[int, str], item_type: int) -> list[db.Message]:
        return [
            db.make_menu_item(0, row_id, name, "", item_type=item_type)
            for row_id, name in sorted(mapping.items(), key=lambda kv: kv[1].lower())
            if name
        ]
