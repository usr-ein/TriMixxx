"""NFS version 2 over UDP (RFC 1094) — RPC program 100003 v2.

Only a handful of procedures matter for reading files off a CDJ: ``LOOKUP`` to
walk a path one component at a time, and ``READ`` to pull byte ranges.
``GETATTR``, ``STATFS``, ``READDIR`` and ``READLINK`` are implemented too, for
experiment E5 -- neither reference client calls them, libcdj's ``READDIR``
attempt came back "procedure unavailable", and the serve side needs to know
which of them real CDJ firmware will call against *us*. ``STATFS`` would also
supply the free/total byte counts the Link-Info panel shows.

Two things to keep in mind:

* **Filenames are UTF-16LE**, not ASCII (see :mod:`prolinks_poc.proto.xdr`).
  A single wrong byte here yields ``NFSERR_NOENT`` and nothing more helpful.
* **Offsets and sizes are 32-bit**, so v2 cannot address beyond 4 GiB. Fine for
  audio, but the ceiling must be asserted rather than silently wrapped.
"""

from __future__ import annotations

import enum
from dataclasses import dataclass

from .errors import ProtocolError
from .xdr import NFS_MAXDATA, XdrReader, XdrWriter

__all__ = [
    "PROGRAM",
    "VERSION",
    "Proc",
    "Stat",
    "FType",
    "FHANDLE_SIZE",
    "MAX_FILE_SIZE",
    "NfsError",
    "Fattr",
    "encode_lookup_args",
    "decode_lookup_result",
    "encode_read_args",
    "decode_read_result",
    "encode_getattr_args",
    "decode_getattr_result",
    "encode_statfs_args",
    "decode_statfs_result",
    "encode_readdir_args",
    "decode_readdir_result",
    "encode_lookup_result",
    "encode_read_result",
]

PROGRAM = 100003
VERSION = 2

#: The standard NFS port, which is also where a real player answers (F6).
PORT = 2049
FHANDLE_SIZE = 32

#: NFSv2 sizes and offsets are unsigned 32-bit.
MAX_FILE_SIZE = 0xFFFFFFFF


class Proc(enum.IntEnum):
    NULL = 0
    GETATTR = 1
    SETATTR = 2
    ROOT = 3  # obsolete
    LOOKUP = 4
    READLINK = 5
    READ = 6
    WRITECACHE = 7  # obsolete
    WRITE = 8
    CREATE = 9
    REMOVE = 10
    RENAME = 11
    LINK = 12
    SYMLINK = 13
    MKDIR = 14
    RMDIR = 15
    READDIR = 16
    STATFS = 17


class Stat(enum.IntEnum):
    """NFSv2 status codes (RFC 1094 §2.3.1)."""

    NFS_OK = 0
    NFSERR_PERM = 1
    NFSERR_NOENT = 2
    NFSERR_IO = 5
    NFSERR_NXIO = 6
    #: The status libcdj hit on MNT. See experiment E2.
    NFSERR_ACCES = 13
    NFSERR_EXIST = 17
    NFSERR_NODEV = 19
    NFSERR_NOTDIR = 20
    NFSERR_ISDIR = 21
    NFSERR_FBIG = 27
    NFSERR_NOSPC = 28
    NFSERR_ROFS = 30
    NFSERR_NAMETOOLONG = 63
    NFSERR_NOTEMPTY = 66
    NFSERR_DQUOT = 69
    #: Filehandle no longer valid -- what we expect after media is swapped.
    #: The signal to re-MNT rather than to give up (experiment E8).
    NFSERR_STALE = 70
    NFSERR_WFLUSH = 99


class FType(enum.IntEnum):
    NFNON = 0
    NFREG = 1
    NFDIR = 2
    NFBLK = 3
    NFCHR = 4
    NFLNK = 5


class NfsError(ProtocolError):
    """A well-formed NFS reply carrying a non-zero status."""

    def __init__(self, status: int, context: str = "") -> None:
        self.status = status
        try:
            name = Stat(status).name
        except ValueError:
            name = f"unknown({status})"
        self.status_name = name
        where = f" ({context})" if context else ""
        super().__init__(f"NFS error {status} {name}{where}")

    @property
    def is_stale(self) -> bool:
        return self.status == Stat.NFSERR_STALE

    @property
    def is_missing(self) -> bool:
        return self.status == Stat.NFSERR_NOENT


@dataclass(frozen=True)
class Fattr:
    """NFSv2 file attributes: 17 consecutive 32-bit fields, 68 bytes.

    Only ``type`` and ``size`` are load-bearing for us -- ``size`` decides how
    many READs to issue and when to stop, so a server that reports it wrongly
    truncates or hangs the transfer. The rest are decoded anyway because they
    cost nothing and the serve side has to emit plausible values for all of
    them.
    """

    type: int
    mode: int
    nlink: int
    uid: int
    gid: int
    size: int
    blocksize: int
    rdev: int
    blocks: int
    fsid: int
    fileid: int
    atime_sec: int
    atime_usec: int
    mtime_sec: int
    mtime_usec: int
    ctime_sec: int
    ctime_usec: int

    WIRE_SIZE = 68

    @property
    def is_directory(self) -> bool:
        return self.type == FType.NFDIR

    @property
    def is_regular(self) -> bool:
        return self.type == FType.NFREG

    @property
    def type_name(self) -> str:
        try:
            return FType(self.type).name
        except ValueError:
            return f"unknown({self.type})"

    @staticmethod
    def decode(reader: XdrReader) -> "Fattr":
        values = [reader.u32() for _ in range(17)]
        return Fattr(*values)

    def encode(self, writer: XdrWriter) -> None:
        for value in (
            self.type, self.mode, self.nlink, self.uid, self.gid, self.size,
            self.blocksize, self.rdev, self.blocks, self.fsid, self.fileid,
            self.atime_sec, self.atime_usec, self.mtime_sec, self.mtime_usec,
            self.ctime_sec, self.ctime_usec,
        ):
            writer.u32(value)

    def __str__(self) -> str:
        return (
            f"{self.type_name} size={self.size} mode={self.mode:o} "
            f"nlink={self.nlink} fileid={self.fileid} mtime={self.mtime_sec}"
        )


# -- LOOKUP ----------------------------------------------------------------


def encode_lookup_args(dir_fhandle: bytes, name: str) -> bytes:
    """``LOOKUP`` arguments: a directory handle plus a **UTF-16LE** name."""
    writer = XdrWriter()
    writer.opaque_fixed(dir_fhandle)
    writer.string_utf16le(name)
    return writer.data()


def decode_lookup_result(data: bytes, context: str = "") -> tuple[bytes, Fattr]:
    reader = XdrReader(data)
    status = reader.u32()
    if status != Stat.NFS_OK:
        raise NfsError(status, context or "LOOKUP")
    fhandle = reader.opaque_fixed(FHANDLE_SIZE)
    return fhandle, Fattr.decode(reader)


def encode_lookup_result(fhandle: bytes, attrs: Fattr, status: int = 0) -> bytes:
    """Server side."""
    writer = XdrWriter()
    writer.u32(status)
    if status == Stat.NFS_OK:
        writer.opaque_fixed(fhandle)
        attrs.encode(writer)
    return writer.data()


# -- READ ------------------------------------------------------------------


def encode_read_args(fhandle: bytes, offset: int, count: int) -> bytes:
    """``READ`` arguments.

    The third field, ``totalcount``, was already deprecated in RFC 1094 and is
    ignored by every server; it is sent as zero, matching both reference
    clients.
    """
    if offset > MAX_FILE_SIZE or offset < 0:
        raise ValueError(f"NFSv2 offset {offset} outside 0..{MAX_FILE_SIZE}")
    writer = XdrWriter()
    writer.opaque_fixed(fhandle)
    writer.u32(offset)
    writer.u32(count)
    writer.u32(0)
    return writer.data()


def decode_read_result(data: bytes, context: str = "") -> tuple[Fattr, bytes]:
    reader = XdrReader(data)
    status = reader.u32()
    if status != Stat.NFS_OK:
        raise NfsError(status, context or "READ")
    attrs = Fattr.decode(reader)
    return attrs, reader.opaque_var(NFS_MAXDATA)


def encode_read_result(attrs: Fattr, payload: bytes, status: int = 0) -> bytes:
    """Server side."""
    writer = XdrWriter()
    writer.u32(status)
    if status == Stat.NFS_OK:
        attrs.encode(writer)
        writer.opaque_var(payload)
    return writer.data()


# -- GETATTR / STATFS / READDIR (experiment E5) ----------------------------


def encode_getattr_args(fhandle: bytes) -> bytes:
    return XdrWriter().opaque_fixed(fhandle).data()


def decode_getattr_result(data: bytes, context: str = "") -> Fattr:
    reader = XdrReader(data)
    status = reader.u32()
    if status != Stat.NFS_OK:
        raise NfsError(status, context or "GETATTR")
    return Fattr.decode(reader)


def encode_statfs_args(fhandle: bytes) -> bytes:
    return XdrWriter().opaque_fixed(fhandle).data()


@dataclass(frozen=True)
class StatfsResult:
    tsize: int
    bsize: int
    blocks: int
    bfree: int
    bavail: int

    @property
    def total_bytes(self) -> int:
        return self.bsize * self.blocks

    @property
    def free_bytes(self) -> int:
        return self.bsize * self.bfree


def decode_statfs_result(data: bytes, context: str = "") -> StatfsResult:
    reader = XdrReader(data)
    status = reader.u32()
    if status != Stat.NFS_OK:
        raise NfsError(status, context or "STATFS")
    return StatfsResult(
        tsize=reader.u32(),
        bsize=reader.u32(),
        blocks=reader.u32(),
        bfree=reader.u32(),
        bavail=reader.u32(),
    )


#: The opaque cookie marking a position in a directory listing. All zeroes
#: means "from the beginning".
READDIR_COOKIE_START = b"\x00\x00\x00\x00"


def encode_readdir_args(
    fhandle: bytes, cookie: bytes = READDIR_COOKIE_START, count: int = 4096
) -> bytes:
    writer = XdrWriter()
    writer.opaque_fixed(fhandle)
    writer.raw(cookie[:4].ljust(4, b"\x00"))
    writer.u32(count)
    return writer.data()


@dataclass(frozen=True)
class DirEntry:
    fileid: int
    name: str
    name_raw: bytes
    cookie: bytes


def decode_readdir_result(
    data: bytes, context: str = "", max_entries: int = 4096
) -> tuple[list[DirEntry], bool]:
    """Decode a ``READDIR`` reply into ``(entries, eof)``.

    Would let us enumerate a CDJ's tree without parsing the pdb first, and
    settle the ``PIONEER`` versus ``.PIONEER`` question (experiment E6) by
    looking rather than guessing. Whether an NXS implements it at all is
    exactly what E5 measures.
    """
    reader = XdrReader(data)
    status = reader.u32()
    if status != Stat.NFS_OK:
        raise NfsError(status, context or "READDIR")
    entries: list[DirEntry] = []
    while reader.boolean():
        fileid = reader.u32()
        name, name_raw = reader.string_utf16le_raw(1024)
        cookie = reader.raw(4)
        entries.append(DirEntry(fileid=fileid, name=name, name_raw=name_raw, cookie=cookie))
        if len(entries) >= max_entries:
            break
    eof = reader.boolean() if reader.remaining() >= 4 else True
    return entries, eof
