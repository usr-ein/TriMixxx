"""UDP socket factories and the recording/guarded channel wrapper.

Every datagram this package sends or receives passes through
:class:`UdpChannel`, which gives three things for free:

* the capture journal sees **everything**, with no call site able to bypass it;
* the passivity guard can veto a send *before* the syscall, which is what makes
  experiment E1's "we transmitted nothing" claim checkable rather than
  aspirational;
* one obvious place to add jitter, rate limiting or fault injection later.

In the Qt port this becomes a thin wrapper over ``QUdpSocket`` with
``readyRead`` connected to :meth:`drain`.
"""

from __future__ import annotations

import logging
import socket
from typing import Callable

log = logging.getLogger(__name__)

__all__ = ["UdpChannel", "djl_socket", "rpc_socket", "MAX_DATAGRAM"]

#: Comfortably above the largest reply we expect. An NFSv2 READ maxes out at
#: 8192 bytes of payload, and a portmap DUMP of a busy host can be a few KB.
MAX_DATAGRAM = 65535


def djl_socket(port: int, bind_ip: str = "0.0.0.0") -> socket.socket:
    """A socket for one of the DJ-Link ports (50000/50001/50002).

    Three flags matter:

    ``SO_REUSEADDR`` / ``SO_REUSEPORT``
        Another tool -- rekordbox, prolink-tools, a second copy of this one, or
        ``tcpdump``'s companion process -- may already hold the port. Without
        these, binding fails outright. ``SO_REUSEPORT`` is the one that
        actually matters on macOS and BSD; it does not exist everywhere, hence
        the guarded ``getattr``.

    ``SO_BROADCAST``
        Required to *send* to the subnet broadcast address (M9). Harmless to
        set on a receive-only socket.

    Binding ``0.0.0.0`` rather than the interface address is deliberate: on
    macOS a socket bound to a specific unicast address does not receive
    subnet-broadcast datagrams, which would silently break discovery -- the
    single most confusing failure mode in this whole layer.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    reuse_port = getattr(socket, "SO_REUSEPORT", None)
    if reuse_port is not None:
        try:
            sock.setsockopt(socket.SOL_SOCKET, reuse_port, 1)
        except OSError:  # pragma: no cover - platform dependent
            log.debug("SO_REUSEPORT unavailable; continuing")
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.setblocking(False)
    sock.bind((bind_ip, port))
    return sock


def rpc_socket(bind_ip: str = "0.0.0.0", port: int = 0) -> socket.socket:
    """An ephemeral socket for ONC RPC calls.

    *bind_ip* should be the address of the interface facing the target, so
    that a multi-homed host sends from the right NIC (see
    :func:`prolinks_poc.net.iface.interface_for_peer`).

    A non-zero *port* below 1024 needs root and exists only to test hypothesis
    H3 of experiment E2 -- that the CDJ's mountd might require a reserved
    source port, which would explain the ``NFSERR_ACCES`` libcdj reported.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setblocking(False)
    sock.bind((bind_ip, port))
    return sock


class UdpChannel:
    """A UDP socket that records, and that a guard can veto sends on."""

    __slots__ = ("sock", "local_port", "recorder", "guard", "label", "_decoder")

    def __init__(
        self,
        sock: socket.socket,
        recorder=None,
        guard=None,
        label: str = "",
        decoder: Callable[[bytes], object] | None = None,
    ) -> None:
        self.sock = sock
        self.local_port = sock.getsockname()[1]
        self.recorder = recorder
        self.guard = guard
        self.label = label or f"udp:{self.local_port}"
        #: Optional callable turning bytes into a JSON-able summary for the
        #: journal. Failures are recorded as ``decode_error`` rather than
        #: raised -- a decoder bug must never cost us the capture.
        self._decoder = decoder

    def fileno(self) -> int:
        return self.sock.fileno()

    def _journal(self, direction: str, peer, data: bytes) -> None:
        if self.recorder is None:
            return
        decoded = None
        error = None
        if self._decoder is not None:
            try:
                decoded = self._decoder(data)
            except Exception as exc:
                error = f"{type(exc).__name__}: {exc}"
        self.recorder.record(direction, self.local_port, peer, data, decoded, error)

    def sendto(self, data: bytes, peer: tuple[str, int]) -> int:
        if self.guard is not None:
            self.guard.check(self.local_port, peer)
        sent = self.sock.sendto(data, peer)
        self._journal("tx", peer, data)
        return sent

    def recv(self) -> tuple[bytes, tuple[str, int]] | None:
        """Read one pending datagram, or ``None`` if the socket would block."""
        try:
            data, peer = self.sock.recvfrom(MAX_DATAGRAM)
        except BlockingIOError:
            return None
        except ConnectionResetError:
            # Windows/macOS can surface a previous send's ICMP port-unreachable
            # here. It refers to an earlier datagram, not this read, so treat
            # it as "nothing to read" rather than as a fatal error.
            return None
        self._journal("rx", peer, data)
        return data, peer

    def drain(self, handler: Callable[[bytes, tuple[str, int]], None], limit: int = 64) -> int:
        """Read up to *limit* pending datagrams, dispatching each to *handler*.

        The cap keeps one very chatty peer from starving the timers -- with
        several CDJs plus a mixer the status port can be busy, and an unbounded
        drain would let it monopolise the loop.
        """
        count = 0
        while count < limit:
            received = self.recv()
            if received is None:
                break
            data, peer = received
            count += 1
            try:
                handler(data, peer)
            except Exception:
                log.exception("%s: handler failed for %sB from %s", self.label, len(data), peer)
        return count

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:  # pragma: no cover
            pass

    def __enter__(self) -> "UdpChannel":
        return self

    def __exit__(self, *exc) -> None:
        self.close()
