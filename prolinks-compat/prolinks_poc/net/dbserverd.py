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
from ..core.medium import Medium
from ..core.slots import MediaSlot
from ..proto import anlz
from ..proto import analysis_wire as wire
from ..proto import dbserver as db
from ..proto.bytes import ByteReader
from ..proto.errors import DecodeError

log = logging.getLogger(__name__)

__all__ = ["DbServer"]


#: How a player asks for per-track analysis, and how a real one answers.
#:
#: ``(response type, ANLZ tag, read the .EXT?, index of the track id, trailing)``
#:
#: Every binary reply shares one envelope, read off a real CDJ-to-CDJ load in
#: ``captures/S06-load-and-play``::
#:
#:     [request type, 0, byte length, blob, *trailing]
#:      uint32        uint32 uint32    binary
#:
#: Two details are easy to get wrong and both cost the load. Argument 0 echoes
#: the **request's message type**, not the track id. And
#: ``GET_WAVEFORM_PREVIEW`` does not carry the track id where its siblings do:
#: its arguments are ``[descriptor, 3, track_id, 0, b""]``, so the id is at
#: index **2**. Reading index 1 asks for analysis of track 3, gets nothing, and
#: answers with an empty blob -- which is what happened.
#:
#: The blobs themselves are **not** the ANLZ bytes; see :mod:`proto.analysis_wire`.
_ANALYSIS_REQUESTS = {
    db.MessageType.GET_WAVEFORM_PREVIEW: (
        db.MessageType.WAVEFORM_PREVIEW, 2, (),
        lambda dat, ext: wire.waveform_preview(dat),
    ),
    db.MessageType.GET_BEAT_GRID: (
        db.MessageType.BEAT_GRID, 1, (0,),
        lambda dat, ext: wire.beat_grid(dat),
    ),
    db.MessageType.GET_WAVEFORM_DETAIL: (
        db.MessageType.WAVEFORM_DETAIL, 1, (),
        lambda dat, ext: wire.waveform_detail(ext),
    ),
    #: The VBR seek index, and the request that most plausibly gates playback:
    #: without a time-to-byte-offset table a player cannot seek in an MP3, so it
    #: has no way to start streaming. Erroring on it is where our loads stopped.
    db.MessageType.GET_VBR_INDEX: (
        db.MessageType.VBR_INDEX, 1, (),
        lambda dat, ext: wire.vbr_index(dat),
    ),
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

    def _medium(self, message: db.Message):
        """The medium this request is about.

        Every request carries an ``r:m:s:t`` descriptor as argument 0 whose third
        byte names the slot, and a player browsing two media on one peer sends
        both down this same connection (F37). So the medium is per *message*,
        never per connection -- caching it here would serve the wrong library the
        moment the DJ switched slots.
        """
        descriptor = message.args[0] if message.args else 0
        if not isinstance(descriptor, int):
            descriptor = 0
        return self.server.medium_for(descriptor)

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

        if message.type in (db.MessageType.UNKNOWN_3100,
                            db.MessageType.UNKNOWN_3D03):
            # For 0x3100 this is what a real deck sends, verbatim. For
            # 0x3d03 it is a guess: no capture contains a reply to it, but it
            # was the only request we still answered with an error, and F25 is
            # the precedent for what erroring on an unknown request costs.
            return [db.Message(
                transaction, db.MessageType.SUCCESS, [message.type, 0],
                arg_types=[db.FieldType.UINT32, db.FieldType.UINT32],
            )]

        if message.type == db.MessageType.GET_ARTWORK:
            image = self._medium(message).artwork_for(message.number(1))
            # A zero-length binary argument is omitted from the wire entirely,
            # so "no artwork" and "here is the artwork" share one shape.
            return [self._binary_reply(transaction, db.MessageType.ARTWORK,
                                       message.type, image)]

        if message.type == db.MessageType.GET_CUE_POINTS:
            # The one reply carrying two blobs: fixed-size cue records, then a
            # (time, loop_time) pair per cue.
            dat, _ext = self._medium(message).analysis_files(message.number(1))
            records, count, times = wire.cue_points(dat)
            return [db.Message(
                transaction, db.MessageType.CUE_POINTS,
                [message.type, 0, len(records), records,
                 wire.CUE_ENTRY_SIZE, count, 0, len(times), times],
                arg_types=[db.FieldType.UINT32] * 3 + [db.FieldType.BINARY]
                          + [db.FieldType.UINT32] * 4 + [db.FieldType.BINARY],
            )]

        analysis = _ANALYSIS_REQUESTS.get(message.type)
        if analysis is not None:
            response_type, id_index, trailing, convert = analysis
            dat, ext = self._medium(message).analysis_files(message.number(id_index))
            return [self._binary_reply(transaction, response_type,
                                       message.type, convert(dat, ext), trailing)]

        if message.type == db.MessageType.RENDER_MENU:
            return self._render(message)

        items = self.server.build_menu(message, self._medium(message))
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

    @staticmethod
    def _binary_reply(transaction, response_type, request_type, blob, trailing=()):
        """The envelope every binary reply shares.

        ``[request type, 0, byte length, blob, *trailing]``. Argument 0 echoes
        the **request's** message type, not the track id -- a real player's
        replies all do, and ours did not.

        A zero-length binary argument is omitted from the wire entirely, so
        "no data" and "here is the data" share one shape and a missing tag
        needs no special case.
        """
        return db.Message(
            transaction, response_type,
            [request_type, 0, len(blob), blob, *trailing],
            arg_types=[db.FieldType.UINT32] * 3 + [db.FieldType.BINARY]
                      + [db.FieldType.UINT32] * len(trailing),
        )

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
        library: Library | dict,
        device_number: int = 5,
        slot: MediaSlot = MediaSlot.USB,
        bind_ip: str = "0.0.0.0",
        port: int = 0,
        query_port: int = db.QUERY_PORT,
        media_root=None,
        recorder=None,
    ) -> None:
        #: slot -> :class:`Medium`. A player browsing two media on one peer uses
        #: a **single** connection and names the slot in every request's
        #: descriptor (F37), so the slot is resolved per request rather than per
        #: server. Passing a bare ``Library`` registers one medium, which is the
        #: single-slot case and most of the tests.
        if isinstance(library, dict):
            self.media: dict[int, Medium] = dict(library)
        else:
            self.media = {
                int(slot): Medium(
                    slot=slot, library=library,
                    root=Path(media_root) if media_root is not None else None,
                )
            }
        self.device_number = device_number
        self.recorder = recorder
        self.stats: dict[str, int] = {}

        self.default_medium = next(iter(self.media.values()))

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

    def medium_for(self, descriptor: int) -> Medium:
        """The medium a request refers to, from its descriptor's slot byte.

        Falls back to the only medium we have when the slot is unknown, which
        keeps a single-slot server answering requests that name any slot --
        the behaviour before two slots existed.
        """
        return self.media.get((descriptor >> 8) & 0xFF, self.default_medium)

    def build_menu(self, message: db.Message, medium: Medium) -> list[db.Message] | None:
        """Turn a menu request into the items it should produce.

        Returns ``None`` for a request we do not implement, which becomes a
        ``0x4003`` error rather than a silent empty list — a player showing an
        empty folder when it should show a failure is worse than an error.
        """
        request_type = message.type

        if request_type == db.MessageType.MENU_ROOT:
            return self._root_menu()
        if request_type == db.MessageType.MENU_TRACK:
            return self._track_list(message.number(1), medium)
        if request_type == db.MessageType.MENU_PLAYLIST:
            return self._playlist_menu(
                message.number(2), bool(message.number(3)), medium
            )
        if request_type in (db.MessageType.GET_METADATA, db.MessageType.GET_GENERIC_METADATA):
            return self._metadata(message.number(1), medium)
        if request_type == db.MessageType.MENU_ARTIST:
            return self._by_name(medium.library.artists, db.ItemType.ARTIST)
        if request_type == db.MessageType.MENU_ALBUM:
            return self._by_name(medium.library.albums, db.ItemType.ALBUM)
        if request_type == db.MessageType.MENU_GENRE:
            return self._by_name(medium.library.genres, db.ItemType.GENRE)
        if request_type == db.MessageType.MENU_KEY:
            return self._by_name(medium.library.keys, db.ItemType.KEY)
        if request_type == db.MessageType.GET_TRACK_INFO:
            return self._track_info(message.number(1), medium)
        if request_type == db.MessageType.MENU_ARTISTS_FOR_GENRE:
            return self._artists_for_genre(message.number(2), medium)
        if request_type == db.MessageType.MENU_ALBUMS_FOR_ARTIST:
            return self._albums_for_artist(message.number(2), medium)
        if request_type == db.MessageType.MENU_TRACKS_FOR_ALBUM:
            return self._tracks_for_album(message.number(2), medium)
        if request_type == db.MessageType.MENU_BITRATE:
            return self._bitrate_menu(medium)
        if request_type == db.MessageType.MENU_SEARCH:
            return self._search(message.string(2), medium)
        return None

    # -- menu construction -----------------------------------------------

    def medium_for(self, descriptor: int) -> Medium:
        """The medium a request refers to, from its descriptor's slot byte.

        Falls back to the only medium we have when the slot is unknown, which
        keeps a single-slot server answering requests that name any slot --
        the behaviour before two slots existed.
        """
        return self.media.get((descriptor >> 8) & 0xFF, self.default_medium)

    def build_menu(self, message: db.Message, medium: Medium) -> list[db.Message] | None:
        """Turn a menu request into the items it should produce.

        Returns ``None`` for a request we do not implement, which becomes a
        ``0x4003`` error rather than a silent empty list — a player showing an
        empty folder when it should show a failure is worse than an error.
        """
        request_type = message.type

        if request_type == db.MessageType.MENU_ROOT:
            return self._root_menu()
        if request_type == db.MessageType.MENU_TRACK:
            return self._track_list(message.number(1), medium)
        if request_type == db.MessageType.MENU_PLAYLIST:
            return self._playlist_menu(
                message.number(2), bool(message.number(3)), medium
            )
        if request_type in (db.MessageType.GET_METADATA, db.MessageType.GET_GENERIC_METADATA):
            return self._metadata(message.number(1), medium)
        if request_type == db.MessageType.MENU_ARTIST:
            return self._by_name(medium.library.artists, db.ItemType.ARTIST)
        if request_type == db.MessageType.MENU_ALBUM:
            return self._by_name(medium.library.albums, db.ItemType.ALBUM)
        if request_type == db.MessageType.MENU_GENRE:
            return self._by_name(medium.library.genres, db.ItemType.GENRE)
        if request_type == db.MessageType.MENU_KEY:
            return self._by_name(medium.library.keys, db.ItemType.KEY)
        if request_type == db.MessageType.GET_TRACK_INFO:
            return self._track_info(message.number(1), medium)
        if request_type == db.MessageType.MENU_ARTISTS_FOR_GENRE:
            return self._artists_for_genre(message.number(2), medium)
        if request_type == db.MessageType.MENU_ALBUMS_FOR_ARTIST:
            return self._albums_for_artist(message.number(2), medium)
        if request_type == db.MessageType.MENU_TRACKS_FOR_ALBUM:
            return self._tracks_for_album(message.number(2), medium)
        if request_type == db.MessageType.MENU_BITRATE:
            return self._bitrate_menu(medium)
        if request_type == db.MessageType.MENU_SEARCH:
            return self._search(message.string(2), medium)
        return None

    def analysis_files(self, track_id: int):
        """The parsed ``.DAT`` and ``.EXT`` for a track, either possibly ``None``.

        Both are read together because a load asks for tags from each within a
        few milliseconds, and parsing a container is walking a tag list -- far
        cheaper than the two file reads it saves. Anything missing or corrupt
        comes back as ``None`` rather than raising: a track analysed by an older
        rekordbox legitimately lacks the newer tags, and a missing waveform
        should cost the waveform, not the load.
        """
        track = medium.library.tracks.get(track_id)
        if track is None or self.media_root is None or not track.analyze_path:
            return None, None

        cached = self._analysis_cache.get(track_id)
        if cached is not None:
            return cached

        def load(relative: str):
            if not relative:
                return None
            try:
                data = (self.media_root / relative.lstrip("/")).read_bytes()
            except OSError:
                log.debug("no analysis file at %s for track %s", relative, track_id)
                return None
            try:
                return anlz.AnlzFile(data)
            except Exception:
                log.debug("could not parse %s", relative)
                return None

        pair = (load(track.analyze_path), load(track.analyze_ext_path))
        self._analysis_cache[track_id] = pair
        return pair

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
    #: Root categories we offer, as ``(item type, label)``. The per-category id
    #: is **derived** from the item type by :data:`db.ROOT_CATEGORY_ID_BIAS`
    #: rather than listed, because deriving it from the *request* type -- F26's
    #: rule -- silently produced the wrong id for KEY and made a deck open
    #: BITRATE instead. See docs/FINDINGS.md F40.
    ROOT_CATEGORIES = (
        (db.ItemType.MENU_GENRE, "GENRE"),
        (db.ItemType.MENU_ARTIST, "ARTIST"),
        (db.ItemType.MENU_ALBUM, "ALBUM"),
        (db.ItemType.MENU_TRACK, "TRACK"),
        (db.ItemType.MENU_PLAYLIST, "PLAYLIST"),
        (db.ItemType.MENU_KEY, "KEY"),
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
            for item_type, label in self.ROOT_CATEGORIES
            for menu_id in (db.root_category_id(item_type),)
        ]

    def _track_list(self, sort: int, medium: Medium | None = None) -> list[db.Message]:
        medium = medium or self.default_medium
        tracks = medium.library.track_list()
        if sort == db.SortOrder.BPM:
            tracks = sorted(tracks, key=lambda t: t.bpm_100)
        elif sort == db.SortOrder.TITLE:
            tracks = sorted(tracks, key=lambda t: t.title.lower())
        return [
            db.make_menu_item(
                0, track.id, track.title, track.artist,
                item_type=db.ItemType.TITLE_AND_ARTIST,
                artwork_id=medium.library.artwork_ids.get(track.id, 0),
            )
            for track in tracks
        ]

    def _playlist_menu(
        self, playlist_id: int, folder: bool, medium: Medium | None = None
    ) -> list[db.Message]:
        medium = medium or self.default_medium
        if folder:
            children = (
                medium.library.root_playlists
                if playlist_id == 0
                else medium.library.playlists[playlist_id].children
                if playlist_id in medium.library.playlists
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
                medium.library.playlist_tracks(playlist_id), start=1
            )
        ]

    def _metadata(self, track_id: int, medium: Medium | None = None) -> list[db.Message]:
        """One track's metadata: **thirteen** items, in a fixed order.

        Modelled field for field on a real deck's reply in
        ``captures/S06-load-and-play``, every value of which was then checked
        against that track's own row in ``export.pdb``.

        Three things ours got wrong, none of which showed up on screen:

        * **four items were missing** -- colour, date added, bitrate and label.
          A player renders the nine it gets and looks perfectly correct;
        * **the referenced ids were the track's own.** An artist item carries
          the *artist's* row id, so the player can offer "more by this artist".
          Ours put the track id in all of them;
        * **the title item carries the artwork id**, and ours left it zero.

        Items are emitted unconditionally, including the empty ones: a real
        deck sends ``label`` with id 0 and no text rather than omitting it, and
        the count is what the client pages against.
        """
        medium = medium or self.default_medium
        track = medium.library.tracks.get(track_id)
        if track is None:
            return []

        def item(first, main_id, item_type, label="", artwork_id=0, flags=0):
            return db.make_menu_item(first, main_id, label, "",
                                     item_type=item_type, artwork_id=artwork_id,
                                     flags=flags)

        # Argument 0 is 1 on eight of the thirteen and 0 on the rest. The split
        # does not line up with anything we can name -- it is not "has a label"
        # (comment has one and gets 0) nor "has a browse menu" (tempo has one
        # and gets 0) -- so it is reproduced as observed rather than derived
        # from a rule we would be inventing.
        return [
            item(1, track.id, db.ItemType.TRACK_TITLE, track.title,
                 artwork_id=track.artwork_id, flags=0x01000000),
            item(1, track.artist_id, db.ItemType.ARTIST, track.artist),
            item(1, track.album_id, db.ItemType.ALBUM, track.album),
            # Numeric fields carry their value in the id, not the label.
            item(0, track.duration, db.ItemType.DURATION),
            item(0, track.bpm_100, db.ItemType.TEMPO),
            item(0, track.id, db.ItemType.COMMENT, track.comment),
            item(1, track.key_id, db.ItemType.KEY, track.key),
            item(0, track.rating, db.ItemType.RATING),
            item(0, track.color_id, db.ItemType.COLOR, track.color),
            item(1, track.genre_id, db.ItemType.GENRE, track.genre),
            item(1, track.id, db.ItemType.DATE_ADDED, track.date_added),
            item(1, track.bitrate, db.ItemType.BITRATE),
            item(1, track.label_id, db.ItemType.LABEL, track.label),
        ]

    def _track_info(self, track_id: int, medium: Medium | None = None) -> list[db.Message]:
        """``GET_TRACK_INFO`` -- **six** items, of which the path is only one.

        Returning the path alone is enough for a player to render the file name
        and to walk it over NFS, and it is what we did. It is not enough for the
        player to *load* the track: a real deck answers with six items, and
        without the rest ours sat at "NOW LOADING..." and then reported that it
        could not decode the format -- having never read a byte of the file, so
        the verdict came from this reply and nowhere else.

**Item 1 is the container, and item 6 is a constant.** Settled by
        capturing one deck loading the format variants from another's USB:
        item 1 held 1 for MP3, 4 for AAC, 11 for WAV and 12 for AIFF -- the pdb
        ``0x5a`` values (F34) -- while item 6 held 1 throughout.

        Two earlier readings were wrong, in opposite directions, and the pair of
        errors cancelled for the only format that had ever been captured. Item 6
        was guessed to be the codec because a format complaint had to come from
        somewhere; item 1 was guessed to be the disc number because the one
        observation was 1 and ``disc_number`` happened to be the only field of
        that track equal to 1. Serving the disc number here is what broke MP3
        loading -- a disc-2 MP3 announces itself as ``AAC``.
        """
        medium = medium or self.default_medium
        track = medium.library.tracks.get(track_id)
        if track is None:
            return []

        def item(main_id, item_type, label="", parent=0):
            return db.make_menu_item(parent, main_id, label, "",
                                     item_type=item_type, flags=0)

        return [
            # Not the title: in *this* reply the 0x04 slot carries the container,
            # with an empty label. A player takes it at face value, so a wrong
            # value here makes it fetch the file and then fail to decode it.
            item(track.file_type, db.ItemType.TRACK_TITLE),
            item(track.duration, db.ItemType.DURATION),
            item(track.bpm_100, db.ItemType.TEMPO),
            item(track.id, db.ItemType.COMMENT, track.comment),
            # Argument 0 is zero on every other menu item ever captured; on this
            # one it is the **file size in bytes**. That is how a player learns
            # how much there is to read -- and it is the one field a load needs
            # that browsing does not, which fits a deck that renders the track
            # perfectly and then cannot open it.
            item(track.id, db.ItemType.PATH, track.path, parent=track.file_size),
            item(1, db.ItemType.UNKNOWN_2F),
        ]

    def _search(self, term: str, medium: Medium | None = None) -> list[db.Message]:
        medium = medium or self.default_medium
        return [
            db.make_menu_item(
                0, track.id, track.title, track.artist,
                item_type=db.ItemType.TITLE_AND_ARTIST,
            )
            for track in medium.library.search(term)
        ]

    # -- drilling into a category ----------------------------------------
    #
    # GENRE -> an artist -> an album -> its tracks. Every one of these came back
    # 0x4003 before, which a deck renders as an EMPTY folder rather than an
    # error, so browsing looked like it worked until you tried to go two levels
    # deep. No capture we have shows a real player *answering* them, so the item
    # shapes below are by analogy with the flat menus -- the requests and their
    # argument positions are observed, the replies are inferred.

    def _artists_for_genre(self, genre_id: int, medium: Medium | None = None):
        medium = medium or self.default_medium
        artist_ids = {
            track.artist_id for track in medium.library.tracks.values()
            if track.genre_id == genre_id
        }
        return self._by_name(
            {i: medium.library.artists.get(i, "") for i in artist_ids},
            db.ItemType.ARTIST,
        )

    def _albums_for_artist(self, artist_id: int, medium: Medium | None = None):
        medium = medium or self.default_medium
        album_ids = {
            track.album_id for track in medium.library.tracks.values()
            if track.artist_id == artist_id
        }
        return self._by_name(
            {i: medium.library.albums.get(i, "") for i in album_ids},
            db.ItemType.ALBUM,
        )

    def _tracks_for_album(self, album_id: int, medium: Medium | None = None):
        medium = medium or self.default_medium
        tracks = [
            track for track in medium.library.tracks.values()
            if track.album_id == album_id
        ]
        # Album order, not alphabetical: a track number is what it is for.
        tracks.sort(key=lambda t: (t.disc_number, t.track_number, t.title.lower()))
        return [
            db.make_menu_item(
                0, track.id, track.title, track.artist,
                item_type=db.ItemType.TITLE_AND_ARTIST,
                artwork_id=medium.library.artwork_ids.get(track.id, 0),
            )
            for track in tracks
        ]

    def _bitrate_menu(self, medium: Medium | None = None):
        """Distinct bitrates, ascending.

        A real server sends the value in the id and leaves both labels empty --
        the deck formats the number itself.
        """
        medium = medium or self.default_medium
        rates = sorted({t.bitrate for t in medium.library.tracks.values() if t.bitrate})
        return [
            db.make_menu_item(0, rate, "", "", item_type=db.ItemType.BITRATE)
            for rate in rates
        ]

    def _by_name(self, mapping: dict[int, str], item_type: int) -> list[db.Message]:
        return [
            db.make_menu_item(0, row_id, name, "", item_type=item_type)
            for row_id, name in sorted(mapping.items(), key=lambda kv: kv[1].lower())
            if name
        ]
