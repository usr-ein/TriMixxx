"""A userspace portmap + mountd + nfsd, over the *same* codecs as the client.

This is milestone M8, and it is worth building early even though serving is
formally Phase C, for three reasons:

1. It exercises every **reply encoder** with no hardware. Our client fetching a
   byte-identical file from our server proves both directions of the whole
   stack.
2. It forces the filehandle design (:mod:`prolinks_poc.net.vfs`) to be settled
   now, while it is cheap to change.
3. It turns objective 2 from greenfield into "swap the VFS backend and bind
   port 111", which is a much smaller thing to have left.

Deliberately incomplete: it answers the procedures a client actually calls,
plus the ones experiment E5 asks about. Write procedures are absent -- CDJ
exports are read-only and so is this.

Binding UDP/111 needs root, so *portmap_port* is configurable. Real CDJs use
the fixed port, and impersonating one eventually means binding it for real.
"""

from __future__ import annotations

import logging

from ..proto import mountd, nfs2, portmap, rpc
from ..proto.errors import DecodeError
from .loop import EventLoop
from .udp import UdpChannel, rpc_socket
from .vfs import Vfs

log = logging.getLogger(__name__)

__all__ = ["NfsServer"]


class NfsServer:
    """Serves one :class:`Vfs` over the three RPC programs a CDJ exposes."""

    def __init__(
        self,
        loop: EventLoop,
        vfs: Vfs,
        exports: dict[str, str] | None = None,
        bind_ip: str = "127.0.0.1",
        portmap_port: int = 0,
        recorder=None,
    ) -> None:
        self.loop = loop
        self.vfs = vfs
        #: export path -> the VFS path it maps to. Defaults mirror a CDJ with
        #: a stick in the USB slot (research/06 §3).
        self.exports = exports if exports is not None else {"/C/": "/"}
        self.stats: dict[str, int] = {}

        self.portmap_channel = UdpChannel(
            rpc_socket(bind_ip, portmap_port), recorder=recorder, label="srv:portmap"
        )
        self.mountd_channel = UdpChannel(
            rpc_socket(bind_ip, 0), recorder=recorder, label="srv:mountd"
        )
        self.nfsd_channel = UdpChannel(
            rpc_socket(bind_ip, 0), recorder=recorder, label="srv:nfsd"
        )

        self.portmap_port = self.portmap_channel.local_port
        self.mountd_port = self.mountd_channel.local_port
        self.nfsd_port = self.nfsd_channel.local_port

    # -- lifecycle -------------------------------------------------------

    def start(self) -> None:
        for channel, dispatch in (
            (self.portmap_channel, self._dispatch_portmap),
            (self.mountd_channel, self._dispatch_mountd),
            (self.nfsd_channel, self._dispatch_nfsd),
        ):
            self.loop.add_reader(channel.sock, self._reader(channel, dispatch))

    def close(self) -> None:
        for channel in (self.portmap_channel, self.mountd_channel, self.nfsd_channel):
            self.loop.remove_reader(channel.sock)
            channel.close()

    def __enter__(self) -> "NfsServer":
        self.start()
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def _reader(self, channel: UdpChannel, dispatch):
        def on_readable() -> None:
            channel.drain(lambda data, peer: self._handle(channel, dispatch, data, peer))

        return on_readable

    def _handle(self, channel: UdpChannel, dispatch, data: bytes, peer) -> None:
        try:
            call = rpc.parse_call(data)
        except DecodeError as exc:
            log.debug("undecodable RPC call from %s: %s", peer, exc)
            return

        # Credentials are accepted whatever they say, matching the observed
        # behaviour of real players (research/06 §2). Experiment E2 questions
        # whether that is really true of the hardware; it is certainly true
        # here, and being permissive keeps us from becoming the reason a
        # client fails.
        try:
            results, accept_stat = dispatch(call)
        except Exception:
            log.exception("handler failed for prog=%s proc=%s", call.program, call.procedure)
            results, accept_stat = b"", rpc.AcceptStat.SYSTEM_ERR

        key = f"{call.program}:{call.procedure}"
        self.stats[key] = self.stats.get(key, 0) + 1
        channel.sendto(rpc.build_reply(call.xid, results, accept_stat), peer)

    # -- program dispatch ------------------------------------------------

    def _dispatch_portmap(self, call: rpc.RpcCall):
        if call.program != portmap.PROGRAM:
            return b"", rpc.AcceptStat.PROG_UNAVAIL
        if call.procedure == portmap.Proc.NULL:
            return b"", rpc.AcceptStat.SUCCESS
        if call.procedure == portmap.Proc.GETPORT:
            from ..proto.xdr import XdrReader, XdrWriter

            reader = XdrReader(call.args)
            program, version = reader.u32(), reader.u32()
            port = {
                (mountd.PROGRAM, mountd.VERSION): self.mountd_port,
                (nfs2.PROGRAM, nfs2.VERSION): self.nfsd_port,
                (portmap.PROGRAM, portmap.VERSION): self.portmap_port,
            }.get((program, version), 0)
            return XdrWriter().u32(port).data(), rpc.AcceptStat.SUCCESS
        if call.procedure == portmap.Proc.DUMP:
            mappings = [
                portmap.Mapping(portmap.PROGRAM, portmap.VERSION,
                                portmap.IPPROTO_UDP, self.portmap_port),
                portmap.Mapping(mountd.PROGRAM, mountd.VERSION,
                                portmap.IPPROTO_UDP, self.mountd_port),
                portmap.Mapping(nfs2.PROGRAM, nfs2.VERSION,
                                portmap.IPPROTO_UDP, self.nfsd_port),
            ]
            return portmap.encode_dump_result(mappings), rpc.AcceptStat.SUCCESS
        return b"", rpc.AcceptStat.PROC_UNAVAIL

    def _dispatch_mountd(self, call: rpc.RpcCall):
        if call.program != mountd.PROGRAM:
            return b"", rpc.AcceptStat.PROG_UNAVAIL
        if call.procedure == mountd.Proc.NULL:
            return b"", rpc.AcceptStat.SUCCESS
        if call.procedure == mountd.Proc.MNT:
            from ..proto.xdr import XdrReader

            path = XdrReader(call.args).string_utf16le()
            if path not in self.exports:
                return (
                    mountd.encode_mnt_result(None, nfs2.Stat.NFSERR_NOENT),
                    rpc.AcceptStat.SUCCESS,
                )
            return (
                mountd.encode_mnt_result(self.vfs.root_handle()),
                rpc.AcceptStat.SUCCESS,
            )
        if call.procedure == mountd.Proc.EXPORT:
            exports = [
                mountd.Export(path=path, path_raw=path.encode("utf-16-le"))
                for path in self.exports
            ]
            return mountd.encode_export_result(exports), rpc.AcceptStat.SUCCESS
        if call.procedure in (mountd.Proc.UMNT, mountd.Proc.UMNTALL):
            return b"", rpc.AcceptStat.SUCCESS
        return b"", rpc.AcceptStat.PROC_UNAVAIL

    def _dispatch_nfsd(self, call: rpc.RpcCall):
        if call.program != nfs2.PROGRAM:
            return b"", rpc.AcceptStat.PROG_UNAVAIL
        if call.procedure == nfs2.Proc.NULL:
            return b"", rpc.AcceptStat.SUCCESS

        from ..proto.xdr import XdrReader, XdrWriter

        if call.procedure == nfs2.Proc.LOOKUP:
            reader = XdrReader(call.args)
            dir_handle = reader.opaque_fixed(nfs2.FHANDLE_SIZE)
            name = reader.string_utf16le()
            found = self.vfs.lookup(dir_handle, name)
            if found is None:
                status = (
                    nfs2.Stat.NFSERR_STALE
                    if self.vfs.resolve(dir_handle) is None
                    else nfs2.Stat.NFSERR_NOENT
                )
                return XdrWriter().u32(status).data(), rpc.AcceptStat.SUCCESS
            handle, node = found
            return (
                nfs2.encode_lookup_result(handle, self.vfs.attrs_for(node)),
                rpc.AcceptStat.SUCCESS,
            )

        if call.procedure == nfs2.Proc.READ:
            reader = XdrReader(call.args)
            handle = reader.opaque_fixed(nfs2.FHANDLE_SIZE)
            offset, count = reader.u32(), reader.u32()
            node = self.vfs.resolve(handle)
            if node is None or node.is_dir:
                return (
                    XdrWriter().u32(nfs2.Stat.NFSERR_STALE).data(),
                    rpc.AcceptStat.SUCCESS,
                )
            payload = self.vfs.read(handle, offset, min(count, 8192)) or b""
            return (
                nfs2.encode_read_result(self.vfs.attrs_for(node), payload),
                rpc.AcceptStat.SUCCESS,
            )

        if call.procedure == nfs2.Proc.GETATTR:
            handle = XdrReader(call.args).opaque_fixed(nfs2.FHANDLE_SIZE)
            node = self.vfs.resolve(handle)
            if node is None:
                return (
                    XdrWriter().u32(nfs2.Stat.NFSERR_STALE).data(),
                    rpc.AcceptStat.SUCCESS,
                )
            writer = XdrWriter().u32(nfs2.Stat.NFS_OK)
            self.vfs.attrs_for(node).encode(writer)
            return writer.data(), rpc.AcceptStat.SUCCESS

        if call.procedure == nfs2.Proc.READDIR:
            reader = XdrReader(call.args)
            handle = reader.opaque_fixed(nfs2.FHANDLE_SIZE)
            node = self.vfs.resolve(handle)
            if node is None or not node.is_dir:
                return (
                    XdrWriter().u32(nfs2.Stat.NFSERR_NOTDIR).data(),
                    rpc.AcceptStat.SUCCESS,
                )
            writer = XdrWriter().u32(nfs2.Stat.NFS_OK)
            for index, (name, child) in enumerate(sorted(node.children.items()), start=1):
                writer.boolean(True)
                writer.u32(child.fileid)
                writer.string_utf16le(name)
                writer.raw(index.to_bytes(4, "big"))
            writer.boolean(False)
            writer.boolean(True)  # eof
            return writer.data(), rpc.AcceptStat.SUCCESS

        if call.procedure == nfs2.Proc.STATFS:
            handle = XdrReader(call.args).opaque_fixed(nfs2.FHANDLE_SIZE)
            if self.vfs.resolve(handle) is None:
                return (
                    XdrWriter().u32(nfs2.Stat.NFSERR_STALE).data(),
                    rpc.AcceptStat.SUCCESS,
                )
            writer = XdrWriter().u32(nfs2.Stat.NFS_OK)
            for value in (8192, 512, 1_000_000, 500_000, 500_000):
                writer.u32(value)
            return writer.data(), rpc.AcceptStat.SUCCESS

        return b"", rpc.AcceptStat.PROC_UNAVAIL
