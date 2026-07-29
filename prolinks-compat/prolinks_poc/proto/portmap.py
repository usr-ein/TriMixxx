"""Portmapper — RPC program 100000 v2, on the fixed UDP port 111.

The only RPC program at a well-known port. Everything else (mountd, nfsd) is
registered dynamically and must be looked up here first.

``rpcinfo`` in the CLI drives this module, and it is the **go/no-go gate** for
the whole project: experiment E4 asks whether a CDJ-2000NXS -- a 2012 unit --
runs an RPC stack at all. The evidence marked "confirmed" in ``research/06``
§1 actually rests on a capture of an *XDJ*, so this must be established on the
real hardware before anything downstream is worth building. Hence ``DUMP`` is
implemented alongside ``GETPORT``: if the portmapper answers but nfsd is
absent, the full registration list distinguishes "no RPC at all" from "RPC but
nothing exported", and those two findings lead to different plans.
"""

from __future__ import annotations

import enum
from dataclasses import dataclass

from .xdr import XdrReader, XdrWriter

__all__ = [
    "PROGRAM",
    "VERSION",
    "PORT",
    "Proc",
    "IPPROTO_TCP",
    "IPPROTO_UDP",
    "Mapping",
    "encode_getport_args",
    "decode_getport_result",
    "decode_dump_result",
    "encode_dump_result",
]

PROGRAM = 100000
VERSION = 2
PORT = 111

IPPROTO_TCP = 6
IPPROTO_UDP = 17


class Proc(enum.IntEnum):
    NULL = 0
    SET = 1
    UNSET = 2
    GETPORT = 3
    DUMP = 4
    CALLIT = 5


@dataclass(frozen=True)
class Mapping:
    """One ``(program, version, protocol, port)`` registration."""

    program: int
    version: int
    protocol: int
    port: int

    @property
    def protocol_name(self) -> str:
        return {IPPROTO_TCP: "tcp", IPPROTO_UDP: "udp"}.get(
            self.protocol, str(self.protocol)
        )

    @property
    def program_name(self) -> str:
        return {
            100000: "portmapper",
            100003: "nfs",
            100005: "mountd",
            100024: "status",
        }.get(self.program, str(self.program))

    def __str__(self) -> str:
        return (
            f"{self.program:>7}  {self.version:>2}  {self.protocol_name:<4} "
            f"{self.port:>6}  {self.program_name}"
        )


def encode_getport_args(
    program: int, version: int, protocol: int = IPPROTO_UDP, port: int = 0
) -> bytes:
    """``GETPORT`` arguments: the mapping to look up, with ``port`` ignored."""
    writer = XdrWriter()
    writer.u32(program)
    writer.u32(version)
    writer.u32(protocol)
    writer.u32(port)
    return writer.data()


def decode_getport_result(data: bytes) -> int:
    """A single port number. **Zero means the program is not registered.**"""
    return XdrReader(data).u32()


def decode_dump_result(data: bytes, max_entries: int = 512) -> list[Mapping]:
    """Decode the ``DUMP`` reply: an XDR linked list of mappings.

    Each element is preceded by a boolean "a value follows"; a false
    terminates the list. The cap guards against a malformed reply looping
    forever.
    """
    reader = XdrReader(data)
    mappings: list[Mapping] = []
    while reader.boolean():
        mappings.append(
            Mapping(
                program=reader.u32(),
                version=reader.u32(),
                protocol=reader.u32(),
                port=reader.u32(),
            )
        )
        if len(mappings) >= max_entries:
            break
    return mappings


def encode_dump_result(mappings) -> bytes:
    """Encode a ``DUMP`` reply. Server side; used by the loopback tests."""
    writer = XdrWriter()
    for mapping in mappings:
        writer.boolean(True)
        writer.u32(mapping.program)
        writer.u32(mapping.version)
        writer.u32(mapping.protocol)
        writer.u32(mapping.port)
    writer.boolean(False)
    return writer.data()
