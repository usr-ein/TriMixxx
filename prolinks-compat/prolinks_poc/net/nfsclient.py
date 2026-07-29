"""The composed NFS client: portmap -> mountd -> nfsd, plus file download.

Reading one file off a CDJ takes four steps (``research/06`` §4):

1. two portmap ``GETPORT`` calls on UDP/111, for mountd and nfsd;
2. ``MNT(export)`` to get the 32-byte root filehandle;
3. ``LOOKUP`` once per path component, feeding each handle into the next;
4. ``READ`` in chunks until the file's reported size is covered.

Ports and root filehandles are cached per client, because redoing portmap and
mount for every file would triple the round trips for no benefit. The root
handle is invalidated on ``NFSERR_STALE``, which is what a media swap looks
like from here (experiment E8).

Chunk sizing is a real decision, not a default: 1280 bytes keeps a reply
datagram (1280 + ~142 B of NFS/RPC/UDP/IP overhead) under a 1500-byte MTU and
avoids IP fragmentation entirely. prolink-connect's 2048 necessarily
fragments; that may well be fine, but it has only been exercised against
XDJ-class hardware, so the conservative value is the default here and
experiment E7 measures whether it costs anything.
"""

from __future__ import annotations

import logging
from collections import deque
from pathlib import Path
from typing import Callable

from ..core.slots import MediaSlot, export_path_for, match_export
from ..proto import mountd, nfs2, portmap, rpc
from ..proto.errors import ProtocolError
from .loop import EventLoop
from .rpcclient import DEFAULT_RETRIES, DEFAULT_TIMEOUT_S, RpcClient, RpcTimeout
from .udp import UdpChannel, rpc_socket

log = logging.getLogger(__name__)

__all__ = ["NfsClient", "DEFAULT_CHUNK", "DEFAULT_WINDOW", "DownloadStats"]

#: Bytes per READ. See the module docstring; research/06 §4.
DEFAULT_CHUNK = 1280
#: READs in flight. research/06 §4 records that going beyond 4 gave the
#: python-prodj-link author no measurable gain, and a modest window is also
#: politer to a player that is busy driving its own display mid-set.
DEFAULT_WINDOW = 4


class DownloadStats:
    """Counters for one transfer, reported by ``prolinks fetch``."""

    __slots__ = ("requests", "short_reads", "bytes_received", "elapsed")

    def __init__(self) -> None:
        self.requests = 0
        self.short_reads = 0
        self.bytes_received = 0
        self.elapsed = 0.0

    @property
    def throughput_kbs(self) -> float:
        return (self.bytes_received / 1024.0 / self.elapsed) if self.elapsed else 0.0

    def __str__(self) -> str:
        return (
            f"{self.bytes_received} bytes in {self.requests} READs "
            f"({self.short_reads} short) in {self.elapsed:.2f}s "
            f"= {self.throughput_kbs:.0f} KiB/s"
        )


class NfsClient:
    """Talks portmap, mount and NFSv2 to one peer."""

    def __init__(
        self,
        loop: EventLoop,
        peer_ip: str,
        local_ip: str = "0.0.0.0",
        recorder=None,
        guard=None,
        credential=None,
        timeout: float = DEFAULT_TIMEOUT_S,
        retries: int = DEFAULT_RETRIES,
        source_port: int = 0,
        portmap_port: int = portmap.PORT,
    ) -> None:
        self.loop = loop
        self.peer_ip = peer_ip
        # Configurable so the loopback server (milestone M8) can be reached
        # without root. Real players always use the fixed port 111.
        self.portmap_port = portmap_port
        channel = UdpChannel(
            rpc_socket(local_ip, source_port),
            recorder=recorder,
            guard=guard,
            label=f"rpc->{peer_ip}",
        )
        self.rpc = RpcClient(
            loop, peer_ip, channel, credential=credential, timeout=timeout, retries=retries
        )
        self._ports: dict[tuple[int, int], int] = {}
        self._roots: dict[str, bytes] = {}

    # -- portmap ---------------------------------------------------------

    def ping_portmap(self) -> bool:
        """Portmap ``NULL``: is anything answering on UDP/111 at all?

        The cheapest possible probe, and the first question experiment E4
        asks. A timeout here means no RPC stack; anything else means there is
        one, which is a completely different situation.
        """
        try:
            self.rpc.call(portmap.PROGRAM, portmap.VERSION, portmap.Proc.NULL,
                          self.portmap_port, b"", label="portmap NULL")
            return True
        except RpcTimeout:
            return False

    def get_port(
        self, program: int, version: int, protocol: int = portmap.IPPROTO_UDP
    ) -> int:
        """Resolve a program to its UDP port. **Zero means not registered.**"""
        cached = self._ports.get((program, version))
        if cached is not None:
            return cached
        reply = self.rpc.call(
            portmap.PROGRAM,
            portmap.VERSION,
            portmap.Proc.GETPORT,
            self.portmap_port,
            portmap.encode_getport_args(program, version, protocol),
            label=f"GETPORT({program},{version})",
        )
        port = portmap.decode_getport_result(reply.results)
        self._ports[(program, version)] = port
        return port

    def dump_portmap(self) -> list[portmap.Mapping]:
        """Everything registered. Distinguishes "no RPC" from "RPC, no NFS"."""
        reply = self.rpc.call(
            portmap.PROGRAM, portmap.VERSION, portmap.Proc.DUMP,
            self.portmap_port, b"", label="portmap DUMP",
        )
        return portmap.decode_dump_result(reply.results)

    @property
    def mountd_port(self) -> int:
        return self.get_port(mountd.PROGRAM, mountd.VERSION)

    @property
    def nfsd_port(self) -> int:
        return self.get_port(nfs2.PROGRAM, nfs2.VERSION)

    # -- mount -----------------------------------------------------------

    def list_exports(self) -> list[mountd.Export]:
        """``EXPORT``. Experiment E3: prefer this over the hardcoded slot table."""
        reply = self.rpc.call(
            mountd.PROGRAM, mountd.VERSION, mountd.Proc.EXPORT,
            self.mountd_port, b"", label="MOUNT EXPORT",
        )
        return mountd.decode_export_result(reply.results)

    def mount(self, export_path: str, refresh: bool = False) -> bytes:
        """``MNT``: export path -> 32-byte root filehandle. Cached."""
        if not refresh and export_path in self._roots:
            return self._roots[export_path]
        reply = self.rpc.call(
            mountd.PROGRAM,
            mountd.VERSION,
            mountd.Proc.MNT,
            self.mountd_port,
            mountd.encode_mnt_args(export_path),
            label=f"MNT({export_path})",
        )
        fhandle = mountd.decode_mnt_result(reply.results, export_path)
        self._roots[export_path] = fhandle
        return fhandle

    def resolve_export(self, slot: MediaSlot) -> str:
        """The export path serving *slot* on this peer.

        Enumerates with ``EXPORT`` and matches by prefix, because the exact
        spelling varies between devices -- ``LinkInfo.pcapng`` shows one player
        mounting ``/C/`` on one peer and ``/C/EXPORT`` on another in the same
        session. Falls back to the documented table if ``EXPORT`` is
        unavailable, so a player that does not implement it still works.
        """
        try:
            exports = self.list_exports()
        except (ProtocolError, RpcTimeout) as exc:
            log.debug("EXPORT unavailable on %s (%s); using the documented path",
                      self.peer_ip, exc)
            return export_path_for(slot)
        matched = match_export(exports, slot)
        if matched is None:
            raise ProtocolError(
                f"{self.peer_ip} does not export {slot.name}; it offers "
                f"{[e.path for e in exports]} (is there media in that slot?)"
            )
        return matched

    def mount_slot(self, slot: MediaSlot, refresh: bool = False) -> bytes:
        return self.mount(self.resolve_export(slot), refresh=refresh)

    def unmount(self, export_path: str) -> None:
        self.rpc.call(
            mountd.PROGRAM, mountd.VERSION, mountd.Proc.UMNT, self.mountd_port,
            mountd.encode_mnt_args(export_path), label=f"UMNT({export_path})",
        )
        self._roots.pop(export_path, None)

    def invalidate(self, export_path: str | None = None) -> None:
        """Drop cached root handles after a stale-handle error or media swap."""
        if export_path is None:
            self._roots.clear()
        else:
            self._roots.pop(export_path, None)

    # -- NFS -------------------------------------------------------------

    def lookup(self, dir_fhandle: bytes, name: str) -> tuple[bytes, nfs2.Fattr]:
        reply = self.rpc.call(
            nfs2.PROGRAM, nfs2.VERSION, nfs2.Proc.LOOKUP, self.nfsd_port,
            nfs2.encode_lookup_args(dir_fhandle, name), label=f"LOOKUP({name})",
        )
        return nfs2.decode_lookup_result(reply.results, f"LOOKUP({name})")

    def lookup_path(
        self, root_fhandle: bytes, path: str
    ) -> tuple[bytes, nfs2.Fattr]:
        """Walk *path* component by component from *root_fhandle*.

        NFS has no notion of a multi-component path: each ``LOOKUP`` resolves
        exactly one name inside one directory. Leading, trailing and doubled
        separators are tolerated so that a path taken straight out of a pdb
        (which may begin with ``/``) can be passed through unchanged.
        """
        fhandle = root_fhandle
        attrs: nfs2.Fattr | None = None
        for component in [part for part in path.replace("\\", "/").split("/") if part]:
            fhandle, attrs = self.lookup(fhandle, component)
        if attrs is None:
            attrs = self.getattr(fhandle)
        return fhandle, attrs

    def read(self, fhandle: bytes, offset: int, count: int) -> tuple[nfs2.Fattr, bytes]:
        reply = self.rpc.call(
            nfs2.PROGRAM, nfs2.VERSION, nfs2.Proc.READ, self.nfsd_port,
            nfs2.encode_read_args(fhandle, offset, count),
            label=f"READ(@{offset},{count})",
        )
        return nfs2.decode_read_result(reply.results, f"READ(@{offset})")

    def getattr(self, fhandle: bytes) -> nfs2.Fattr:
        reply = self.rpc.call(
            nfs2.PROGRAM, nfs2.VERSION, nfs2.Proc.GETATTR, self.nfsd_port,
            nfs2.encode_getattr_args(fhandle), label="GETATTR",
        )
        return nfs2.decode_getattr_result(reply.results)

    def statfs(self, fhandle: bytes) -> nfs2.StatfsResult:
        reply = self.rpc.call(
            nfs2.PROGRAM, nfs2.VERSION, nfs2.Proc.STATFS, self.nfsd_port,
            nfs2.encode_statfs_args(fhandle), label="STATFS",
        )
        return nfs2.decode_statfs_result(reply.results)

    def readdir(
        self, fhandle: bytes, cookie: bytes = nfs2.READDIR_COOKIE_START, count: int = 4096
    ) -> tuple[list[nfs2.DirEntry], bool]:
        reply = self.rpc.call(
            nfs2.PROGRAM, nfs2.VERSION, nfs2.Proc.READDIR, self.nfsd_port,
            nfs2.encode_readdir_args(fhandle, cookie, count), label="READDIR",
        )
        return nfs2.decode_readdir_result(reply.results)

    # -- download --------------------------------------------------------

    def download(
        self,
        fhandle: bytes,
        size: int,
        chunk: int = DEFAULT_CHUNK,
        window: int = DEFAULT_WINDOW,
        on_progress: Callable[[int, int], None] | None = None,
        stats: DownloadStats | None = None,
    ) -> bytes:
        """Fetch a whole file with *window* READs in flight.

        There is no streaming in NFSv2, so the client drives everything: it
        tracks the next offset to request and the next to commit, stashes
        out-of-order replies by offset, and flushes contiguous runs as they
        become available. Replies are matched to requests by RPC XID inside
        :class:`~prolinks_poc.net.rpcclient.RpcClient`.

        Short reads are handled explicitly rather than assumed away. A server
        is entitled to return fewer bytes than asked for; the shortfall is
        re-requested at the resulting offset, which keeps the committed stream
        gap-free without needing the reply sizes to be uniform.
        """
        if size == 0:
            return b""
        if size > nfs2.MAX_FILE_SIZE:
            raise ValueError(
                f"file is {size} bytes; NFSv2 offsets are 32-bit and cannot "
                f"address beyond {nfs2.MAX_FILE_SIZE}"
            )

        stats = stats if stats is not None else DownloadStats()
        started = self.loop.now()

        blocks: dict[int, bytes] = {}
        gaps: deque[tuple[int, int]] = deque()
        next_offset = 0
        write_offset = 0
        output = bytearray()
        in_flight = 0
        failure: list[Exception] = []

        def flush() -> None:
            nonlocal write_offset
            while write_offset in blocks:
                block = blocks.pop(write_offset)
                output.extend(block)
                write_offset += len(block)
            if on_progress is not None:
                on_progress(write_offset, size)

        def on_reply(offset: int, requested: int):
            def handler(reply, error) -> None:
                nonlocal in_flight
                in_flight -= 1
                if error is not None:
                    failure.append(error)
                    return
                try:
                    reply.raise_for_status(f"READ(@{offset})")
                    _attrs, data = nfs2.decode_read_result(
                        reply.results, f"READ(@{offset})"
                    )
                except Exception as exc:
                    failure.append(exc)
                    return

                blocks[offset] = data
                stats.bytes_received += len(data)
                if len(data) < requested and offset + len(data) < size:
                    stats.short_reads += 1
                    gaps.append((offset + len(data), requested - len(data)))
                flush()
                pump()

            return handler

        def pump() -> None:
            nonlocal next_offset, in_flight
            while in_flight < window and not failure:
                if gaps:
                    offset, count = gaps.popleft()
                elif next_offset < size:
                    offset = next_offset
                    count = min(chunk, size - next_offset)
                    next_offset += count
                else:
                    return
                in_flight += 1
                stats.requests += 1
                self.rpc.call_async(
                    nfs2.PROGRAM,
                    nfs2.VERSION,
                    nfs2.Proc.READ,
                    self.nfsd_port,
                    nfs2.encode_read_args(fhandle, offset, count),
                    on_reply(offset, count),
                    label=f"READ(@{offset},{count})",
                )

        pump()
        # Worst case: every chunk needs every retry. Wide, but it only bounds
        # a pathological case -- the normal exit is the predicate below.
        chunks = max(1, -(-size // chunk))
        budget = self.rpc.timeout * (self.rpc.retries + 1) * chunks / max(1, window) + 5.0
        self.loop.run_until(
            predicate=lambda: bool(failure) or write_offset >= size,
            deadline=self.loop.now() + budget,
        )

        stats.elapsed = self.loop.now() - started
        if failure:
            self.rpc.cancel_all()
            raise failure[0]
        if write_offset < size:
            self.rpc.cancel_all()
            raise RpcTimeout(
                f"download stalled at {write_offset}/{size} bytes from {self.peer_ip}"
            )
        return bytes(output)

    def download_to_file(
        self,
        fhandle: bytes,
        size: int,
        destination: Path,
        chunk: int = DEFAULT_CHUNK,
        window: int = DEFAULT_WINDOW,
        on_progress: Callable[[int, int], None] | None = None,
        stats: DownloadStats | None = None,
    ) -> Path:
        """Download to *destination*, via a ``.part`` file renamed on success.

        The rename is atomic, so the destination path either does not exist or
        holds a complete file -- never a truncated one. That matters more in
        Mixxx than here: ``SoundSourceProxy`` sniffs file *content* to decide
        whether a track is playable, so a half-written download would be
        classified as an unsupported format rather than as an incomplete file.
        """
        destination = Path(destination)
        destination.parent.mkdir(parents=True, exist_ok=True)
        partial = destination.with_suffix(destination.suffix + ".part")

        data = self.download(fhandle, size, chunk, window, on_progress, stats)
        partial.write_bytes(data)
        partial.replace(destination)
        return destination

    def close(self) -> None:
        self.rpc.close()

    def __enter__(self) -> "NfsClient":
        return self

    def __exit__(self, *exc) -> None:
        self.close()
