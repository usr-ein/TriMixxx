"""MOUNT protocol — RPC program 100005 v1, on a portmap-discovered UDP port.

``MNT`` turns an export path into the 32-byte root filehandle that every
subsequent NFS ``LOOKUP`` starts from. ``EXPORT`` lists what is available.

Two experiments live here:

**E3 — are the export names right on an NXS?** ``research/06`` §3 gives
SD=``/B/``, USB=``/C/``, rekordbox=``/``, agreed by both reference clients --
but both were validated against XDJ-class hardware. So :func:`decode_export_result`
returns the **raw UTF-16LE bytes alongside** the decoded string: what the
hardware actually says must be recorded verbatim, not merely as our reading of
it. Given a working ``EXPORT``, driving mounts from its output beats trusting
the hardcoded table, and the serve side needs the same code anyway.

**E2 — the unexplained ``NFSERR_ACCES``.** libcdj reported mount failing with
status 13. The credential flavour is the leading hypothesis, which is why the
RPC layer makes it selectable; this module is deliberately agnostic about it.
"""

from __future__ import annotations

import enum
from dataclasses import dataclass

from .errors import ProtocolError
from .xdr import XdrReader, XdrWriter

__all__ = [
    "PROGRAM",
    "VERSION",
    "Proc",
    "FHANDLE_SIZE",
    "MountError",
    "Export",
    "encode_mnt_args",
    "decode_mnt_result",
    "encode_mnt_result",
    "decode_export_result",
    "encode_export_result",
]

PROGRAM = 100005
VERSION = 1

#: The port a real player answers on. Not a registered number -- discover it
#: through portmap rather than assuming it. Recorded because three independent
#: observations across three devices all gave 48276 (F6), which makes it stable
#: enough to serve on when impersonating a player (experiment E9).
PIONEER_PORT = 48276

#: NFSv2 filehandles are exactly 32 opaque bytes (RFC 1094). Treat the
#: contents as an uninterpreted token and echo them back byte for byte.
FHANDLE_SIZE = 32


class Proc(enum.IntEnum):
    NULL = 0
    MNT = 1
    DUMP = 2
    UMNT = 3
    UMNTALL = 4
    EXPORT = 5


class MountError(ProtocolError):
    """``MNT`` returned a non-zero status."""

    def __init__(self, status: int, path: str) -> None:
        self.status = status
        self.path = path
        super().__init__(f"MNT({path!r}) failed with status {status} ({_status_name(status)})")


def _status_name(status: int) -> str:
    # MOUNT reuses the NFS error numbering.
    return {
        0: "OK",
        1: "NFSERR_PERM",
        2: "NFSERR_NOENT",
        5: "NFSERR_IO",
        13: "NFSERR_ACCES",
        20: "NFSERR_NOTDIR",
        63: "NFSERR_NAMETOOLONG",
        70: "NFSERR_STALE",
    }.get(status, f"unknown({status})")


@dataclass(frozen=True)
class Export:
    """One entry from the ``EXPORT`` list."""

    path: str
    #: The literal UTF-16LE bytes, kept for experiment E3.
    path_raw: bytes
    groups: tuple[str, ...] = ()

    def __str__(self) -> str:
        groups = f"  groups={list(self.groups)}" if self.groups else ""
        return f"{self.path!r}  raw={self.path_raw.hex()}{groups}"


def encode_mnt_args(export_path: str) -> bytes:
    """``MNT`` argument: the export path as a **UTF-16LE** string.

    Not ASCII. See :mod:`prolinks_poc.proto.xdr`.
    """
    return XdrWriter().string_utf16le(export_path).data()


def decode_mnt_result(data: bytes, path: str = "") -> bytes:
    """Return the 32-byte root filehandle, or raise :class:`MountError`.

    Wire form is an XDR union discriminated on the status: zero is followed by
    the filehandle, anything else by nothing.
    """
    reader = XdrReader(data)
    status = reader.u32()
    if status != 0:
        raise MountError(status, path)
    return reader.opaque_fixed(FHANDLE_SIZE)


def encode_mnt_result(fhandle: bytes | None, status: int = 0) -> bytes:
    """Server side: encode an ``MNT`` reply."""
    writer = XdrWriter()
    writer.u32(status)
    if status == 0:
        if fhandle is None or len(fhandle) != FHANDLE_SIZE:
            raise ValueError(f"filehandle must be exactly {FHANDLE_SIZE} bytes")
        writer.opaque_fixed(fhandle)
    return writer.data()


def decode_export_result(data: bytes, max_entries: int = 64) -> list[Export]:
    """Decode the ``EXPORT`` reply: an XDR linked list of export nodes.

    Each node is ``bool value_follows``, then the directory path, then a
    linked list of group names, then the next node.

    **The two string fields use different encodings.** The directory path is
    UTF-16LE, per Pioneer's convention; the group names are plain **ASCII**.
    This was originally implemented as UTF-16LE for both, on the assumption
    that the convention applied uniformly -- a real capture showed otherwise,
    decoding ``169.254.244.181/255.255.255.255`` as CJK mojibake. See
    docs/FINDINGS.md, correction C7.

    Each group is a ``host/netmask`` pair naming a client permitted to mount,
    so a player exports its media specifically to the peers it has discovered
    rather than to the world.
    """
    reader = XdrReader(data)
    exports: list[Export] = []
    while reader.boolean():
        path, path_raw = reader.string_utf16le_raw(1024)
        groups: list[str] = []
        while reader.boolean():
            groups.append(reader.string_ascii(1024))
            if len(groups) > max_entries:
                break
        exports.append(Export(path=path, path_raw=path_raw, groups=tuple(groups)))
        if len(exports) >= max_entries:
            break
    return exports


def encode_export_result(exports) -> bytes:
    """Server side: encode an ``EXPORT`` reply."""
    writer = XdrWriter()
    for export in exports:
        writer.boolean(True)
        writer.string_utf16le(export.path)  # UTF-16LE
        for group in export.groups:
            writer.boolean(True)
            writer.string_ascii(group)  # ...but ASCII. See decode_export_result.
        writer.boolean(False)
    writer.boolean(False)
    return writer.data()
