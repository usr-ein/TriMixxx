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
from pathlib import Path

from ..core.library import Library
from ..core.slots import MediaSlot
from ..proto import anlz
from ..proto import dbserver as db
from ..proto.bytes import ByteReader
from ..proto.errors import DecodeError

log = logging.getLogger(__name__)

__all__ = ["DbServer"]


#: dbserver analysis requests -> (response type, ANLZ tag, read the .EXT?).
#: A player asks for these when loading a track and appears to abort the load
#: if they are unavailable -- browsing works without them, loading does not.
_ANALYSIS_REQUESTS = {
    db.MessageType.GET_WAVEFORM_PREVIEW:
        (db.MessageType.WAVEFORM_PREVIEW, anlz.TAG_WAVEFORM_PREVIEW, False),
    db.MessageType.GET_BEAT_GRID:
        (db.MessageType.BEAT_GRID, anlz.TAG_BEAT_GRID, False),
    db.MessageType.GET_CUE_POINTS:
        (db.MessageType.CUE_POINTS, anlz.TAG_CUES, False),
    db.MessageType.GET_WAVEFORM_DETAIL:
        (db.MessageType.WAVEFORM_DETAIL, anlz.TAG_WAVEFORM_DETAIL, True),
    db.MessageType.GET_CUE_POINTS_EXT:
        (db.MessageType.CUE_POINTS_EXT, anlz.TAG_CUES_EXT, True),
}


class _Connection(threading.Thread):
    """One client conversation. Owns the menu result set being paged through."""

    def __init__(self, server: "DbServer", sock: socket.socket, peer) -> None:
        super().__init__(daemon=True, name=f"dbserver-{peer[0]}")
        self.server = server
        self.sock = sock
        self.peer = peer
        self.buffer = b""
        #: Result sets awaiting render, keyed by item count.
        #:
        #: A client does **not** run one menu at a time. A real CDJ browsing a
        #: track list interleaves per-track ``GET_METADATA`` lookups with
        #: continued scrolling of the list, then resumes rendering the list at
        #: the next offset without re-issuing the menu request. Holding a
        #: single result set meant the metadata replaced the track list and
        #: every later page came back empty -- which presented as the list
        #: going blank part-way down.
        #:
        #: The render request's ``total`` argument is what distinguishes them
        #: (692 for the track list, 8 for a metadata lookup), so that is the
        #: key. Two concurrent menus of identical length would still collide;
        #: nothing observed does that, and the fallback below covers it.
        self.menus: dict[int, list[db.Message]] = {}
        self.last_menu: list[db.Message] = []
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

        if message.type == db.MessageType.MENU_CLOSE:
            # Fire-and-forget: a real player sends this after finishing with a
            # menu and expects nothing back. Replying at all -- let alone with
            # the 0x4003 error an unhandled type would have produced -- risks
            # desynchronising a client that is not listening for one.
            #
            # Deliberately does NOT discard the result sets. "Release that
            # menu" was an inference from its position in the stream, and
            # acting on it broke pagination: a deck sends this while still
            # scrolling the list it is supposedly finished with.
            return []

        if message.type == db.MessageType.UNKNOWN_3E03:
            # Modelled on a real reply captured between two players:
            # 0x4b02 with [request type, 0, responder device number, ""].
            # Its meaning is unknown; erroring on it is what stopped a deck
            # from browsing past our root menu (FINDINGS F25).
            return [db.Message(
                transaction, db.MessageType.UNKNOWN_4B02,
                [message.type, 0, self.server.device_number, ""],
                arg_types=[db.FieldType.UINT32, db.FieldType.UINT32,
                           db.FieldType.UINT32, db.FieldType.STRING],
            )]

        if message.type == db.MessageType.GET_ARTWORK:
            image = self.server.artwork_for(message.number(1))
            # A zero-length binary argument is omitted from the wire entirely,
            # so "no artwork" and "here is the artwork" share one shape.
            return [db.Message(
                transaction, db.MessageType.ARTWORK,
                [message.number(1), 0, len(image), image],
                arg_types=[db.FieldType.UINT32, db.FieldType.UINT32,
                           db.FieldType.UINT32, db.FieldType.BINARY],
            )]

        analysis = _ANALYSIS_REQUESTS.get(message.type)
        if analysis is not None:
            response_type, fourcc, use_ext = analysis
            payload = self.server.analysis_for(message.number(1), fourcc, use_ext)
            return [db.Message(
                transaction, response_type,
                [message.number(1), len(payload), payload],
                arg_types=[db.FieldType.UINT32, db.FieldType.UINT32,
                           db.FieldType.BINARY],
            )]

        if message.type == db.MessageType.RENDER_MENU:
            return self._render(message)

        items = self.server.build_menu(message)
        if items is None:
            log.info("unsupported request %s from %s", message.type_name, self.peer[0])
            return [db.Message(transaction, db.MessageType.ERROR, [message.type, 0])]

        # Establish the result set, then answer with its size. The client
        # follows up with 0x3000 to page through it -- possibly interleaved
        # with other menus, hence keying by size rather than replacing.
        self.menus[len(items)] = items
        self.last_menu = items
        return [db.Message(transaction, db.MessageType.SUCCESS,
                           [message.type, len(items)])]

    def _render(self, message: db.Message) -> list[db.Message]:
        offset = message.number(1)
        limit = message.number(2)
        total = message.number(4)
        # Pick the result set the client is actually paging through. It tells
        # us which by echoing that menu's size in the total argument.
        items = self.menus.get(total, self.last_menu)
        window = items[offset : offset + limit]

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
        media_root=None,
        recorder=None,
    ) -> None:
        self.library = library
        #: Root of the served medium, so artwork can be read off it.
        self.media_root = Path(media_root) if media_root is not None else None
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

    def analysis_for(self, track_id: int, fourcc: bytes, use_ext: bool) -> bytes:
        """One ANLZ tag for a track, read off the served medium.

        Returns empty for anything we cannot supply -- a track analysed by an
        older rekordbox genuinely lacks the newer tags, and the protocol has a
        representation for an empty blob.
        """
        track = self.library.tracks.get(track_id)
        if track is None or self.media_root is None:
            return b""
        relative = track.analyze_ext_path if use_ext else track.analyze_path
        if not relative:
            return b""
        try:
            data = (self.media_root / relative.lstrip("/")).read_bytes()
        except OSError:
            log.debug("no analysis file at %s for track %s", relative, track_id)
            return b""
        try:
            return anlz.AnlzFile(data).tag_payload(fourcc)
        except Exception:
            log.debug("could not parse %s", relative)
            return b""

    def artwork_for(self, artwork_id: int) -> bytes:
        """The album-art image for *artwork_id*, or empty if we have none.

        The pdb maps the id to a path on the medium; since we are serving that
        medium we can simply read it. Returns empty rather than raising for a
        missing file -- a track without art is ordinary, and the protocol has a
        representation for it.
        """
        path = self.library.artwork.get(artwork_id)
        if not path or self.media_root is None:
            return b""
        candidate = self.media_root / path.lstrip("/")
        try:
            return candidate.read_bytes()
        except OSError:
            log.debug("artwork %s not readable at %s", artwork_id, candidate)
            return b""

    #: Root-menu categories: item type, label, and the id a real player puts in
    #: argument 2 -- the low byte of the corresponding menu request type
    #: (GENRE 0x1001 -> 1, ARTIST 0x1002 -> 2, PLAYLIST 0x1105 -> 5).
    ROOT_CATEGORIES = (
        (db.ItemType.MENU_GENRE, "GENRE", db.MessageType.MENU_GENRE & 0xFF),
        (db.ItemType.MENU_ARTIST, "ARTIST", db.MessageType.MENU_ARTIST & 0xFF),
        (db.ItemType.MENU_ALBUM, "ALBUM", db.MessageType.MENU_ALBUM & 0xFF),
        (db.ItemType.MENU_TRACK, "TRACK", db.MessageType.MENU_TRACK & 0xFF),
        (db.ItemType.MENU_PLAYLIST, "PLAYLIST", db.MessageType.MENU_PLAYLIST & 0xFF),
        (db.ItemType.MENU_KEY, "KEY", db.MessageType.MENU_KEY & 0xFF),
    )

    def _root_menu(self) -> list[db.Message]:
        """Which browse categories we claim to offer.

        Three details are copied from a real player's root menu rather than
        invented, because a deck that renders our labels perfectly well will
        still refuse to open a category if they are wrong (FINDINGS F26):

        * argument 2 carries a per-category id, not zero;
        * the label is wrapped in U+FFFA/U+FFFB;
        * argument 8 is zero, not the ``0x01000000`` that a track item carries.
        """
        return [
            db.make_menu_item(
                0, menu_id, db.menu_label(label), "",
                item_type=item_type, flags=0,
            )
            for item_type, label, menu_id in self.ROOT_CATEGORIES
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
                artwork_id=self.library.artwork_ids.get(track.id, 0),
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
