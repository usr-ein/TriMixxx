"""ONC RPC v2 over UDP (RFC 1057) — message framing for portmap/mount/NFS.

One request per datagram, one reply per datagram, correlated by a 32-bit XID.

Both directions are implemented. The call encoder and reply decoder are what
the client needs; the call *decoder* and reply *encoder* are what the loopback
server in milestone M8 needs, and building them now is what turns the eventual
serve side (objective 2) into plumbing rather than a rewrite. They also give
the capture tooling a free dissector.

**Authentication.** Calls carry ``AUTH_UNIX`` credentials with an
``AUTH_NULL`` verifier. ``research/06`` §2 records that CDJs do not appear to
enforce credentials -- but marks that *inferred*, because both reference
clients send ``AUTH_UNIX`` and neither ever tried ``AUTH_NULL``. Experiment E2
tests exactly that, since libcdj's unexplained ``NFSERR_ACCES`` is most easily
explained by its use of glibc's ``clnt_create()``, which defaults to
``AUTH_NULL``. Hence both flavours are implemented and selectable.
"""

from __future__ import annotations

import enum
import os
from dataclasses import dataclass

from .errors import DecodeError, ProtocolError
from .xdr import XdrReader, XdrWriter

__all__ = [
    "RPC_VERSION",
    "MsgType",
    "ReplyStat",
    "AcceptStat",
    "RejectStat",
    "AuthFlavor",
    "AuthUnix",
    "AUTH_NULL_CRED",
    "STAMP_OBSERVED_CDJ",
    "STAMP_DEADBEEF",
    "random_stamp",
    "RpcCall",
    "RpcReply",
    "build_call",
    "parse_call",
    "build_reply",
    "parse_reply",
    "RpcCallFailed",
]

RPC_VERSION = 2


class MsgType(enum.IntEnum):
    CALL = 0
    REPLY = 1


class ReplyStat(enum.IntEnum):
    MSG_ACCEPTED = 0
    MSG_DENIED = 1


class AcceptStat(enum.IntEnum):
    SUCCESS = 0
    PROG_UNAVAIL = 1
    PROG_MISMATCH = 2
    PROC_UNAVAIL = 3
    GARBAGE_ARGS = 4
    SYSTEM_ERR = 5


class RejectStat(enum.IntEnum):
    RPC_MISMATCH = 0
    AUTH_ERROR = 1


class AuthFlavor(enum.IntEnum):
    AUTH_NULL = 0
    AUTH_UNIX = 1
    AUTH_SHORT = 2
    AUTH_DES = 3


#: The first stamp seen in dysentery's LinkInfo capture, and the value
#: prolink-connect hardcodes believing it to be a magic constant. It is not:
#: every call in that capture carries a different stamp (FINDINGS C8). Kept
#: as a fixed default for reproducible tests and captures.
STAMP_OBSERVED_CDJ = 0x967B8703
#: The stamp python-prodj-link uses. Kept so experiment E2 can A/B them.
STAMP_DEADBEEF = 0xDEADBEEF


def random_stamp() -> int:
    """A fresh nonce for the ``AUTH_UNIX`` stamp.

    Real players emit a different value on every call, so a client that
    wants to look like one should too. Servers do not appear to inspect it.
    """
    return int.from_bytes(os.urandom(4), "big")


class RpcCallFailed(ProtocolError):
    """An RPC reply that was denied, or accepted with a non-SUCCESS status."""


@dataclass(frozen=True)
class AuthUnix:
    """``AUTH_UNIX`` credential body (RFC 1057 §9.2)."""

    stamp: int = STAMP_OBSERVED_CDJ
    machine_name: str = ""
    uid: int = 0
    gid: int = 0
    gids: tuple[int, ...] = ()

    def encode(self) -> bytes:
        writer = XdrWriter()
        writer.u32(self.stamp)
        writer.string_ascii(self.machine_name)
        writer.u32(self.uid)
        writer.u32(self.gid)
        writer.array_u32(self.gids)
        return writer.data()

    @staticmethod
    def decode(data: bytes) -> "AuthUnix":
        reader = XdrReader(data)
        return AuthUnix(
            stamp=reader.u32(),
            machine_name=reader.string_ascii(255),
            uid=reader.u32(),
            gid=reader.u32(),
            gids=tuple(reader.array_u32()),
        )

    @property
    def flavor(self) -> AuthFlavor:
        return AuthFlavor.AUTH_UNIX


class _AuthNull:
    """The empty ``AUTH_NULL`` credential/verifier."""

    flavor = AuthFlavor.AUTH_NULL

    def encode(self) -> bytes:
        return b""

    def __repr__(self) -> str:  # pragma: no cover
        return "AUTH_NULL"


AUTH_NULL_CRED = _AuthNull()


def _write_auth(writer: XdrWriter, credential) -> None:
    writer.u32(int(credential.flavor))
    writer.opaque_var(credential.encode())


def _read_auth(reader: XdrReader) -> tuple[int, bytes]:
    flavor = reader.u32()
    return flavor, reader.opaque_var(400)  # RFC 1057 caps auth bodies at 400B


@dataclass(frozen=True)
class RpcCall:
    xid: int
    program: int
    version: int
    procedure: int
    cred_flavor: int
    cred_body: bytes
    verf_flavor: int
    verf_body: bytes
    args: bytes


@dataclass(frozen=True)
class RpcReply:
    xid: int
    accepted: bool
    accept_stat: int | None
    reject_stat: int | None
    results: bytes
    #: Populated on a PROG_MISMATCH rejection, which carries the server's
    #: supported version range -- the difference between "wrong version" and
    #: "not there at all", which matters for experiment E4.
    low_version: int | None = None
    high_version: int | None = None

    @property
    def ok(self) -> bool:
        return self.accepted and self.accept_stat == AcceptStat.SUCCESS

    def raise_for_status(self, context: str = "") -> "RpcReply":
        if self.ok:
            return self
        where = f" ({context})" if context else ""
        if not self.accepted:
            name = _enum_name(RejectStat, self.reject_stat)
            if self.reject_stat == RejectStat.RPC_MISMATCH:
                raise RpcCallFailed(
                    f"RPC call denied{where}: RPC_MISMATCH, server supports "
                    f"versions {self.low_version}..{self.high_version}"
                )
            raise RpcCallFailed(f"RPC call denied{where}: {name}")
        name = _enum_name(AcceptStat, self.accept_stat)
        if self.accept_stat == AcceptStat.PROG_MISMATCH:
            raise RpcCallFailed(
                f"RPC call failed{where}: PROG_MISMATCH, server supports "
                f"versions {self.low_version}..{self.high_version}"
            )
        raise RpcCallFailed(f"RPC call failed{where}: {name}")


def _enum_name(enum_class, value) -> str:
    try:
        return enum_class(value).name
    except (ValueError, TypeError):
        return f"unknown({value})"


# -- client side -----------------------------------------------------------


def build_call(
    xid: int,
    program: int,
    version: int,
    procedure: int,
    args: bytes = b"",
    credential=None,
) -> bytes:
    """Encode an RPC CALL datagram."""
    writer = XdrWriter()
    writer.u32(xid)
    writer.u32(MsgType.CALL)
    writer.u32(RPC_VERSION)
    writer.u32(program)
    writer.u32(version)
    writer.u32(procedure)
    _write_auth(writer, credential if credential is not None else AUTH_NULL_CRED)
    _write_auth(writer, AUTH_NULL_CRED)  # verifier is always AUTH_NULL here
    writer.raw(args)
    return writer.data()


def parse_reply(data: bytes) -> RpcReply:
    """Decode an RPC REPLY datagram. Raises :class:`DecodeError` if malformed."""
    reader = XdrReader(data)
    xid = reader.u32()
    msg_type = reader.u32()
    if msg_type != MsgType.REPLY:
        raise DecodeError(f"expected an RPC REPLY, got msg_type {msg_type}")

    reply_stat = reader.u32()
    if reply_stat == ReplyStat.MSG_DENIED:
        reject_stat = reader.u32()
        low = high = None
        if reject_stat == RejectStat.RPC_MISMATCH:
            low, high = reader.u32(), reader.u32()
        return RpcReply(
            xid=xid,
            accepted=False,
            accept_stat=None,
            reject_stat=reject_stat,
            results=b"",
            low_version=low,
            high_version=high,
        )
    if reply_stat != ReplyStat.MSG_ACCEPTED:
        raise DecodeError(f"invalid reply_stat {reply_stat}")

    _read_auth(reader)  # server verifier, always AUTH_NULL in practice
    accept_stat = reader.u32()
    low = high = None
    if accept_stat == AcceptStat.PROG_MISMATCH:
        low, high = reader.u32(), reader.u32()
    return RpcReply(
        xid=xid,
        accepted=True,
        accept_stat=accept_stat,
        reject_stat=None,
        results=reader.all_remaining(),
        low_version=low,
        high_version=high,
    )


# -- server side (milestone M8 and, later, objective 2) --------------------


def parse_call(data: bytes) -> RpcCall:
    """Decode an RPC CALL datagram."""
    reader = XdrReader(data)
    xid = reader.u32()
    msg_type = reader.u32()
    if msg_type != MsgType.CALL:
        raise DecodeError(f"expected an RPC CALL, got msg_type {msg_type}")
    rpc_version = reader.u32()
    if rpc_version != RPC_VERSION:
        raise DecodeError(f"unsupported RPC version {rpc_version}")
    program, version, procedure = reader.u32(), reader.u32(), reader.u32()
    cred_flavor, cred_body = _read_auth(reader)
    verf_flavor, verf_body = _read_auth(reader)
    return RpcCall(
        xid=xid,
        program=program,
        version=version,
        procedure=procedure,
        cred_flavor=cred_flavor,
        cred_body=cred_body,
        verf_flavor=verf_flavor,
        verf_body=verf_body,
        args=reader.all_remaining(),
    )


def build_reply(
    xid: int,
    results: bytes = b"",
    accept_stat: int = AcceptStat.SUCCESS,
    low_version: int | None = None,
    high_version: int | None = None,
) -> bytes:
    """Encode an accepted RPC REPLY."""
    writer = XdrWriter()
    writer.u32(xid)
    writer.u32(MsgType.REPLY)
    writer.u32(ReplyStat.MSG_ACCEPTED)
    _write_auth(writer, AUTH_NULL_CRED)
    writer.u32(accept_stat)
    if accept_stat == AcceptStat.PROG_MISMATCH:
        writer.u32(low_version or 0)
        writer.u32(high_version or 0)
    writer.raw(results)
    return writer.data()


def build_denied_reply(xid: int, reject_stat: int = RejectStat.AUTH_ERROR) -> bytes:
    """Encode a denied RPC REPLY."""
    writer = XdrWriter()
    writer.u32(xid)
    writer.u32(MsgType.REPLY)
    writer.u32(ReplyStat.MSG_DENIED)
    writer.u32(reject_stat)
    if reject_stat == RejectStat.RPC_MISMATCH:
        writer.u32(RPC_VERSION)
        writer.u32(RPC_VERSION)
    return writer.data()
