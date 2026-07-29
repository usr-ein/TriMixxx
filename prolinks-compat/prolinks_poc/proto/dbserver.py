"""dbserver / "remotedb" — the TCP metadata protocol players use to browse each other.

This is what a CDJ actually drives when you press **LINK** and browse another
player's media: dysentery's ``menus.adoc:19`` says requesting the root menu "is
what a player will do when you use the Link button", and the ``LinkInfo``
captures show the 12523 port-discovery handshake followed by a conversation on
1051. So serving this is what makes our library *browsable* by real hardware;
NFS alone only makes the files readable.

Implemented from ``research/04``. Both directions, as everywhere in this
package: the client half reads other players' libraries, the server half is
objective 2.

Three things about the wire format are easy to get wrong:

* **Two independent type numberings.** Every value is a tagged field
  (``0f``/``10``/``11``/``14``/``26``), and the header *also* carries a 12-byte
  blob of *argument* tags (``02``/``03``/``06``) describing the same arguments
  with different numbers. Both must agree or the peer rejects the message.
* **Strings count characters, not bytes.** The length prefix is the UTF-16
  character count *including a trailing NUL*, so the bytes on the wire are
  twice that. The text is UTF-16 **big**-endian -- the opposite of the
  UTF-16LE used by the NFS layer, and of the UTF-16BE-inside-a-little-endian-
  file used by pdb PioStrings.
* **A zero-length binary argument is omitted entirely.** Not sent as a
  zero-length blob: absent. The preceding UInt32 length argument is how you
  know. Reading one blindly desynchronises the whole message.
"""

from __future__ import annotations

import enum
from dataclasses import dataclass, field

from .bytes import ByteReader, ByteWriter
from .errors import DecodeError

__all__ = [
    "MAGIC",
    "QUERY_PORT",
    "DEFAULT_DBSERVER_PORT",
    "PORT_QUERY_PACKET",
    "PREAMBLE",
    "decode_messages",
    "decode_message",
    "FieldType",
    "ArgType",
    "MessageType",
    "ItemType",
    "MenuTarget",
    "TrackType",
    "SortOrder",
    "Message",
    "encode_field",
    "decode_field",
    "descriptor",
    "make_introduce",
    "make_render",
    "make_menu_request",
    "menu_label",
]

#: Every dbserver message starts with this UInt32.
MAGIC = 0x872349AE

#: Fixed port answering the "which port is the dbserver on?" question.
QUERY_PORT = 12523
#: Always this in practice, but it is documented as dynamic, so ask.
DEFAULT_DBSERVER_PORT = 1051

#: The 19-byte port-discovery query: a big-endian length, the ASCII name, a NUL.
PORT_QUERY_PACKET = (
    (15).to_bytes(4, "big") + b"RemoteDBServer" + b"\x00"
)

#: The transaction id reserved for Introduce and Disconnect.
SETUP_TRANSACTION_ID = 0xFFFFFFFE


#: research/04 §4.3: 64 items per render is documented safe on Nexus 2, and
#: thousands demonstrably fail. Some hardware tolerates more, but the safe
#: batch size is hardware-dependent and this costs nothing.
MAX_RENDER_BATCH = 64


class FieldType(enum.IntEnum):
    """The tag byte that precedes every value."""

    UINT8 = 0x0F
    UINT16 = 0x10
    UINT32 = 0x11
    BINARY = 0x14
    STRING = 0x26


class ArgType(enum.IntEnum):
    """The *other* numbering, used only in the header's 12-byte type blob."""

    STRING = 0x02
    BINARY = 0x03
    UINT8 = 0x04  # inferred; never observed
    UINT16 = 0x05  # inferred; never observed
    UINT32 = 0x06


#: The connection preamble: a bare UInt32 field with value 1, which the
#: server echoes verbatim before any message is exchanged (research/04 §2.1).
#: It heads the byte stream in *both* directions, so a stream decoder must
#: step over it before looking for the first magic.
PREAMBLE = bytes([FieldType.UINT32]) + (1).to_bytes(4, "big")


_FIELD_TO_ARG = {
    FieldType.UINT8: ArgType.UINT8,
    FieldType.UINT16: ArgType.UINT16,
    FieldType.UINT32: ArgType.UINT32,
    FieldType.BINARY: ArgType.BINARY,
    FieldType.STRING: ArgType.STRING,
}


class MessageType(enum.IntEnum):
    """Message type IDs (``research/04`` §3.4). Requests then responses."""

    INTRODUCE = 0x0000
    #: Undocumented, and sent by a real CDJ-2000NXS during an ordinary LINK
    #: browse (23 times in one session). Zero arguments, reuses the transaction
    #: id of the ``RENDER_MENU`` it immediately follows, and draws **no reply at
    #: all** -- every other request type in that capture is exactly accounted
    #: for by the responses. Most likely "done with that menu, release its
    #: state", which fits a protocol the docs describe as stateful per client.
    #: The name is our inference; the wire behaviour is observed. FINDINGS F16.
    MENU_CLOSE = 0x0001
    DISCONNECT = 0x0100

    RENDER_MENU = 0x3000

    MENU_ROOT = 0x1000
    MENU_GENRE = 0x1001
    MENU_ARTIST = 0x1002
    MENU_ALBUM = 0x1003
    MENU_TRACK = 0x1004
    MENU_BPM = 0x1006
    MENU_RATING = 0x1007
    MENU_YEAR = 0x1008
    MENU_LABEL = 0x100A
    MENU_COLOR = 0x100D
    MENU_TIME = 0x1010
    MENU_BITRATE = 0x1011
    MENU_HISTORY = 0x1012
    MENU_FILENAME = 0x1013
    MENU_KEY = 0x1014
    MENU_ARTISTS_FOR_GENRE = 0x1101
    MENU_ALBUMS_FOR_ARTIST = 0x1102
    MENU_TRACKS_FOR_ALBUM = 0x1103
    MENU_PLAYLIST = 0x1105
    MENU_SEARCH = 0x1300
    MENU_FOLDER = 0x2006

    GET_METADATA = 0x2002
    GET_ARTWORK = 0x2003
    GET_WAVEFORM_PREVIEW = 0x2004
    GET_TRACK_INFO = 0x2102
    GET_CUE_POINTS = 0x2104
    GET_GENERIC_METADATA = 0x2202
    GET_BEAT_GRID = 0x2204
    GET_WAVEFORM_DETAIL = 0x2904
    GET_CUE_POINTS_EXT = 0x2B04
    GET_ANALYSIS_TAG = 0x2C04
    #: Undocumented. A player sends it mid-load, between ``GET_TRACK_INFO``
    #: and the analysis fetches, and a real deck answers with a bare
    #: ``SUCCESS`` echoing the type. Four arguments: descriptor, track id,
    #: 0, 0. docs/FINDINGS.md F30.
    UNKNOWN_3100 = 0x3100
    #: Undocumented, and the likely gate on playback: a real deck answers
    #: with 1604 bytes, exactly the size of a ``PVBR`` payload -- the MP3
    #: variable-bitrate seek index. Without a time-to-byte-offset table a
    #: player cannot seek in the file, which would explain a load that
    #: resolves the path and then never issues a single READ.
    GET_VBR_INDEX = 0x2504
    #: Undocumented. Sent immediately after ``Introduce`` by a player browsing
    #: a *foreign* device -- it does not appear between two CDJs. One argument,
    #: the r:m:s:t descriptor. Answering it with an error makes the player
    #: fetch the root menu and then disconnect without drilling in, which is
    #: how it presented: the categories listed, every one of them empty.
    #: FINDINGS F25.
    UNKNOWN_3E03 = 0x3E03
    #: Undocumented, two arguments (descriptor, track id), sent once
    #: during playback. Never appears between two real CDJs, so like
    #: :attr:`UNKNOWN_3E03` it is something a player asks only of a
    #: foreign device -- and no capture shows what a real answer looks
    #: like. We acknowledge it the way a deck acknowledges ``0x3100``,
    #: because erroring on an unknown request is what stopped browsing
    #: dead in F25 and it is the last ERROR we still emit. **Guessed.**
    UNKNOWN_3D03 = 0x3D03

    SUCCESS = 0x4000
    MENU_HEADER = 0x4001
    ARTWORK = 0x4002
    ERROR = 0x4003
    MENU_ITEM = 0x4101
    MENU_FOOTER = 0x4201
    WAVEFORM_PREVIEW = 0x4402
    BEAT_GRID = 0x4602
    CUE_POINTS = 0x4702
    WAVEFORM_DETAIL = 0x4A02
    CUE_POINTS_EXT = 0x4E02
    ANALYSIS_TAG = 0x4F02
    #: The reply to :attr:`UNKNOWN_3E03`. Four arguments, observed as
    #: ``[0x3e03, 0, <our device number>, ""]``.
    UNKNOWN_4B02 = 0x4B02
    #: The reply to :attr:`GET_VBR_INDEX`.
    VBR_INDEX = 0x4502


class ItemType(enum.IntEnum):
    """Menu-item kinds, carried in argument 7 (``research/04`` §4.5).

    CDJ-3000s pack extra information into the two high bytes, so mask with
    ``0xffff`` before comparing -- see :func:`item_type_of`.
    """

    PATH = 0x0000
    FOLDER = 0x0001
    ALBUM = 0x0002
    DISC = 0x0003
    TRACK_TITLE = 0x0004
    GENRE = 0x0006
    ARTIST = 0x0007
    PLAYLIST = 0x0008
    RATING = 0x000A
    DURATION = 0x000B
    TEMPO = 0x000D
    LABEL = 0x000E
    KEY = 0x000F
    BITRATE = 0x0010
    #: Observed in a real metadata reply carrying the track's ``color_id``.
    COLOR = 0x0013
    YEAR = 0x0011
    COMMENT = 0x0023
    HISTORY_PLAYLIST = 0x0024
    ORIGINAL_ARTIST = 0x0028
    REMIXER = 0x0029
    DATE_ADDED = 0x002E
    #: Undocumented, and the sixth item of a ``GET_TRACK_INFO`` reply.
    #: Observed once, carrying ``1``. The likeliest candidate for a codec
    #: identifier: a deck that never read a byte of the file still
    #: complained it could not decode the format, so the judgement came
    #: from that reply. Unconfirmed. docs/FINDINGS.md F31.
    UNKNOWN_2F = 0x002F
    ALL = 0x00A0
    MENU_GENRE = 0x0080
    MENU_ARTIST = 0x0081
    MENU_ALBUM = 0x0082
    MENU_TRACK = 0x0083
    MENU_PLAYLIST = 0x0084
    MENU_KEY = 0x008B
    TITLE_AND_ARTIST = 0x0704


#: Real players wrap root-menu category labels in U+FFFA (interlinear
#: annotation anchor) and U+FFFB (terminator) -- ``\ufffaPLAYLIST\ufffb``.
#: Presumably a marker letting the player substitute a localised string for a
#: known category. A bare label renders, but the deck then declines to open the
#: category (FINDINGS F26).
MENU_LABEL_PREFIX = "\ufffa"
MENU_LABEL_SUFFIX = "\ufffb"


def menu_label(text: str) -> str:
    """Wrap a root-menu category label the way real hardware does."""
    return f"{MENU_LABEL_PREFIX}{text}{MENU_LABEL_SUFFIX}"


def item_type_of(raw: int) -> int:
    """Mask off the CDJ-3000 high bytes (``research/04`` §4.5)."""
    return raw & 0xFFFF


class MenuTarget(enum.IntEnum):
    """Byte ``M`` of the ``r:m:s:t`` descriptor -- where the result is shown."""

    MAIN = 0x01
    SUB = 0x02
    PREVIEW = 0x03
    #: Used for binary loads: artwork, beat grid, preview waveform, cues.
    BINARY = 0x08


class TrackType(enum.IntEnum):
    """Byte ``T`` of the descriptor."""

    REKORDBOX = 1
    UNANALYZED = 2
    CD_AUDIO = 5
    STREAMING = 6


class SortOrder(enum.IntEnum):
    """Argument 2 of a track-list request (``research/04`` §4.6).

    Also selects the second column returned in each item, which is why
    ``DEFAULT`` is not simply "unsorted".
    """

    DEFAULT = 0x00
    TITLE = 0x01
    ARTIST = 0x02
    ALBUM = 0x03
    BPM = 0x04
    RATING = 0x05
    GENRE = 0x06
    COMMENT = 0x07
    TIME = 0x08
    REMIXER = 0x09
    LABEL = 0x0A
    ORIGINAL_ARTIST = 0x0B
    KEY = 0x0C
    BITRATE = 0x0D
    PLAY_COUNT = 0x10
    DATE_ADDED = 0x11


def descriptor(
    device_number: int,
    slot: int,
    menu: int = MenuTarget.MAIN,
    track_type: int = TrackType.REKORDBOX,
) -> int:
    """Pack the ``r:m:s:t`` UInt32 that opens nearly every request.

    ``D << 24 | M << 16 | Sr << 8 | Tr`` (``research/04`` §4.1). ``D`` is *our*
    device number, which is why dbserver queries need a number in 1-4 and why
    the safe observer number 7 cannot be used for them.
    """
    return (
        (device_number & 0xFF) << 24
        | (menu & 0xFF) << 16
        | (slot & 0xFF) << 8
        | (track_type & 0xFF)
    )


# -- fields ----------------------------------------------------------------


def encode_field(field_type: int, value) -> bytes:
    """Encode one tagged field."""
    writer = ByteWriter().u8(field_type)
    if field_type == FieldType.UINT8:
        writer.u8(value)
    elif field_type == FieldType.UINT16:
        writer.u16(value)
    elif field_type == FieldType.UINT32:
        writer.u32(value)
    elif field_type == FieldType.BINARY:
        writer.u32(len(value)).raw(value)
    elif field_type == FieldType.STRING:
        # The prefix counts UTF-16 characters *including* the trailing NUL,
        # so a 3-character string sends 4 and 8 bytes of text.
        encoded = (value + "\x00").encode("utf-16-be")
        writer.u32(len(encoded) // 2).raw(encoded)
    else:
        raise ValueError(f"unknown field type {field_type:#04x}")
    return writer.data()


def decode_field(reader: ByteReader) -> tuple[int, object]:
    """Decode one tagged field, returning ``(field_type, value)``."""
    field_type = reader.u8()
    if field_type == FieldType.UINT8:
        return field_type, reader.u8()
    if field_type == FieldType.UINT16:
        return field_type, reader.u16()
    if field_type == FieldType.UINT32:
        return field_type, reader.u32()
    if field_type == FieldType.BINARY:
        length = reader.u32()
        if length > reader.remaining():
            raise DecodeError(
                f"binary field claims {length} bytes, {reader.remaining()} remain"
            )
        return field_type, reader.raw(length)
    if field_type == FieldType.STRING:
        characters = reader.u32()
        if characters * 2 > reader.remaining():
            raise DecodeError(
                f"string field claims {characters} chars ({characters * 2} bytes), "
                f"{reader.remaining()} remain"
            )
        text = reader.raw(characters * 2).decode("utf-16-be", errors="replace")
        return field_type, text.rstrip("\x00")
    raise DecodeError(f"unknown dbserver field type {field_type:#04x}")


# -- messages --------------------------------------------------------------


@dataclass
class Message:
    """One dbserver message: a header plus up to 12 tagged arguments."""

    transaction_id: int
    type: int
    args: list = field(default_factory=list)
    #: Parallel to ``args``: the :class:`FieldType` of each. Inferred from the
    #: Python types when not given, which is right for everything except
    #: distinguishing the integer widths.
    arg_types: list = field(default_factory=list)

    def __post_init__(self) -> None:
        if not self.arg_types:
            self.arg_types = [_infer_field_type(value) for value in self.args]

    # -- convenience accessors --

    def number(self, index: int, default: int = 0) -> int:
        value = self.args[index] if index < len(self.args) else default
        return value if isinstance(value, int) else default

    def string(self, index: int, default: str = "") -> str:
        value = self.args[index] if index < len(self.args) else default
        return value if isinstance(value, str) else default

    def blob(self, index: int) -> bytes:
        value = self.args[index] if index < len(self.args) else b""
        return value if isinstance(value, (bytes, bytearray)) else b""

    @property
    def type_name(self) -> str:
        try:
            return MessageType(self.type).name
        except ValueError:
            return f"0x{self.type:04x}"

    def encode(self) -> bytes:
        writer = ByteWriter()
        writer.raw(encode_field(FieldType.UINT32, MAGIC))
        writer.raw(encode_field(FieldType.UINT32, self.transaction_id))
        writer.raw(encode_field(FieldType.UINT16, self.type))
        writer.raw(encode_field(FieldType.UINT8, len(self.args)))

        tags = bytearray(12)
        for index, field_type in enumerate(self.arg_types[:12]):
            tags[index] = _FIELD_TO_ARG[field_type]
        writer.raw(encode_field(FieldType.BINARY, bytes(tags)))

        for field_type, value in zip(self.arg_types, self.args):
            # A zero-length binary argument is omitted from the wire entirely
            # (research/04 §3.3). The preceding UInt32 length argument is what
            # tells the reader it is absent.
            if field_type == FieldType.BINARY and not value:
                continue
            writer.raw(encode_field(field_type, value))
        return writer.data()

    def __str__(self) -> str:
        rendered = []
        for value in self.args:
            if isinstance(value, (bytes, bytearray)):
                rendered.append(f"<{len(value)}B>")
            elif isinstance(value, str):
                rendered.append(repr(value))
            else:
                rendered.append(f"{value:#x}" if value > 9 else str(value))
        return f"{self.type_name}(tx={self.transaction_id:#x}) [{', '.join(rendered)}]"


def _infer_field_type(value) -> int:
    if isinstance(value, str):
        return FieldType.STRING
    if isinstance(value, (bytes, bytearray)):
        return FieldType.BINARY
    return FieldType.UINT32


_ARG_TO_FIELD = {
    ArgType.STRING: FieldType.STRING,
    ArgType.BINARY: FieldType.BINARY,
    ArgType.UINT8: FieldType.UINT8,
    ArgType.UINT16: FieldType.UINT16,
    ArgType.UINT32: FieldType.UINT32,
}


def decode_message(reader: ByteReader) -> Message:
    """Decode one message from *reader*, leaving it positioned after it.

    Raises :class:`DecodeError` on anything malformed -- including a truncated
    message, which is the normal case when reading a partially-arrived TCP
    buffer, so callers treat that as "wait for more" rather than as an error.
    """
    field_type, magic = decode_field(reader)
    if field_type != FieldType.UINT32 or magic != MAGIC:
        raise DecodeError(f"bad dbserver magic {magic:#x}")

    _, transaction_id = decode_field(reader)
    _, message_type = decode_field(reader)
    _, arg_count = decode_field(reader)
    _, tags = decode_field(reader)

    if arg_count > 12:
        raise DecodeError(f"implausible dbserver argument count {arg_count}")

    args: list = []
    arg_types: list = []
    for index in range(arg_count):
        arg_tag = tags[index] if index < len(tags) else 0
        expected = _ARG_TO_FIELD.get(arg_tag)
        if expected is None:
            raise DecodeError(f"unknown argument tag {arg_tag:#04x} at index {index}")

        # The omitted-empty-blob rule: a binary argument whose length was
        # announced as 0 by the preceding UInt32 simply is not there.
        if expected == FieldType.BINARY and args and args[-1] == 0:
            args.append(b"")
            arg_types.append(FieldType.BINARY)
            continue

        actual_type, value = decode_field(reader)
        args.append(value)
        arg_types.append(actual_type)

    return Message(
        transaction_id=transaction_id, type=message_type, args=args, arg_types=arg_types
    )


def decode_messages(data: bytes, skip_preamble: bool = True) -> tuple[list[Message], int]:
    """Decode as many whole messages as *data* contains.

    Returns the messages and the number of bytes consumed, so a TCP reader can
    keep the remainder and try again when more arrives -- messages are not
    framed by anything but their own contents, so a partial trailing message is
    normal and simply left unconsumed.

    *skip_preamble* steps over the leading 5-byte handshake field that both
    peers send once at the start of the connection.
    """
    start = len(PREAMBLE) if skip_preamble and data.startswith(PREAMBLE) else 0
    reader = ByteReader(data, start)
    messages: list[Message] = []
    consumed = start
    while not reader.at_end():
        try:
            messages.append(decode_message(reader))
        except DecodeError:
            break
        consumed = reader.pos
    return messages, consumed


# -- request builders ------------------------------------------------------


def make_introduce(device_number: int) -> Message:
    """The handshake message. Reply is ``0x4000`` carrying *their* number."""
    return Message(
        transaction_id=SETUP_TRANSACTION_ID,
        type=MessageType.INTRODUCE,
        args=[device_number],
    )


def make_disconnect() -> Message:
    return Message(
        transaction_id=SETUP_TRANSACTION_ID, type=MessageType.DISCONNECT, args=[]
    )


def make_render(
    transaction_id: int, desc: int, offset: int, limit: int, total: int | None = None
) -> Message:
    """The paginating follow-up to any menu request (``research/04`` §4.3)."""
    return Message(
        transaction_id=transaction_id,
        type=MessageType.RENDER_MENU,
        args=[desc, offset, limit, 0, total if total is not None else limit, 0],
    )


def make_menu_request(
    transaction_id: int, message_type: int, desc: int, *extra_args: int
) -> Message:
    """Any menu request: descriptor first, then the type-specific arguments."""
    return Message(
        transaction_id=transaction_id,
        type=message_type,
        args=[desc, *extra_args],
    )


def make_menu_item(
    parent_id: int,
    main_id: int,
    label1: str,
    label2: str = "",
    item_type: int = ItemType.TRACK_TITLE,
    artwork_id: int = 0,
    playlist_position: int = 0,
    flags: int = 0x01000000,
) -> Message:
    """Build a ``0x4101`` menu item -- the server side of a browse.

    Always 12 arguments in a fixed order (``research/04`` §4.4). Arguments 3
    and 5 are the *byte* lengths of the two labels, which is twice the
    character count plus the NUL, and getting them wrong is one of the ways a
    real player will refuse to render the list.

    **Argument 10 tracks argument 7.** Across all 1,700 menu items in the
    reference captures the two are never independent: an item carrying
    ``flags = 0x01000000`` also carries ``0x100`` here, and an item with zero
    flags has zero here. Both are non-zero only on the two kinds of item that
    name a track -- ``TITLE_AND_ARTIST`` list rows and ``TRACK_TITLE``. We had
    been sending argument 10 as zero unconditionally, so every track row we
    served was subtly unlike a real one. Deriving it removes the chance of
    setting one and forgetting the other.
    """
    return Message(
        transaction_id=0,  # overwritten by the caller with the request's id
        type=MessageType.MENU_ITEM,
        args=[
            parent_id,
            main_id,
            (len(label1) + 1) * 2,
            label1,
            (len(label2) + 1) * 2,
            label2,
            item_type,
            flags,
            artwork_id,
            playlist_position,
            0x100 if flags else 0,
            0,
        ],
        arg_types=[
            FieldType.UINT32, FieldType.UINT32, FieldType.UINT32, FieldType.STRING,
            FieldType.UINT32, FieldType.STRING, FieldType.UINT32, FieldType.UINT32,
            FieldType.UINT32, FieldType.UINT32, FieldType.UINT32, FieldType.UINT32,
        ],
    )
