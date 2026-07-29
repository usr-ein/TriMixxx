"""ONC RPC client: XID correlation, timeouts and retries over UDP.

UDP gives no delivery guarantee and no ordering, so the client owns both. Every
call gets a unique XID; replies are matched back to their outstanding call by
that XID and by nothing else. A call with no reply by its deadline is
retransmitted verbatim -- the same XID, so a late original and a retry reply
are recognised as duplicates of one another rather than as two answers.

The design is event-driven throughout, with :meth:`call` layered on top as a
convenience that drives the loop until its own reply arrives. That ordering
matters: the windowed download in :mod:`prolinks_poc.net.nfsclient` needs
several requests genuinely in flight at once, which a blocking-only client
could not express. It is also the shape the Qt port wants, where ``RpcClient``
is a ``QObject`` on the network thread with a 250 ms retry ``QTimer``.
"""

from __future__ import annotations

import logging
from dataclasses import dataclass, replace
from typing import Callable

from ..proto import rpc
from ..proto.errors import DecodeError
from .loop import EventLoop
from .udp import UdpChannel

log = logging.getLogger(__name__)

__all__ = ["RpcClient", "RpcTimeout", "DEFAULT_TIMEOUT_S", "DEFAULT_RETRIES"]

#: research/06 §4: python-prodj-link retries a request after 2 s, up to 5
#: times. Generous, but a CDJ that is busy driving its own display can be slow,
#: and a spurious give-up looks identical to a real failure in the results.
DEFAULT_TIMEOUT_S = 2.0
DEFAULT_RETRIES = 5
#: How often to sweep for expired calls. Fine enough not to add meaningful
#: latency to a 2 s timeout, coarse enough to be free.
RETRY_TICK_S = 0.25


class RpcTimeout(TimeoutError):
    """No reply after every retry was exhausted."""


ReplyHandler = Callable[[rpc.RpcReply | None, Exception | None], None]


@dataclass
class _PendingCall:
    xid: int
    port: int
    datagram: bytes
    handler: ReplyHandler | None
    deadline: float
    attempts: int = 1
    label: str = ""


class RpcClient:
    """Sends RPC calls to one peer, over one socket."""

    def __init__(
        self,
        loop: EventLoop,
        peer_ip: str,
        channel: UdpChannel,
        credential=None,
        timeout: float = DEFAULT_TIMEOUT_S,
        retries: int = DEFAULT_RETRIES,
        randomise_stamp: bool = True,
    ) -> None:
        self.loop = loop
        self.peer_ip = peer_ip
        self.channel = channel
        self.credential = credential if credential is not None else rpc.AuthUnix()
        #: Real players send a fresh AUTH_UNIX stamp on every call
        #: (FINDINGS C8), so by default we do too. Disable for byte-stable
        #: captures when diffing our traffic against a reference run.
        self.randomise_stamp = randomise_stamp
        self.timeout = timeout
        self.retries = retries

        self._pending: dict[int, _PendingCall] = {}
        # Start XIDs at an arbitrary non-zero point so that a stray reply from
        # a previous run is unlikely to collide with a fresh call.
        self._next_xid = 0x1000_0000
        self.stats = {"calls": 0, "replies": 0, "retries": 0, "timeouts": 0, "unmatched": 0}

        loop.add_reader(channel.sock, self._on_readable)
        self._retry_timer = loop.call_every(RETRY_TICK_S, self._on_retry_tick)

    # -- issuing ---------------------------------------------------------

    def _allocate_xid(self) -> int:
        xid = self._next_xid
        self._next_xid = (self._next_xid + 1) & 0xFFFFFFFF
        return xid

    def call_async(
        self,
        program: int,
        version: int,
        procedure: int,
        port: int,
        args: bytes = b"",
        handler: ReplyHandler | None = None,
        label: str = "",
    ) -> int:
        """Send a call and return immediately with its XID."""
        xid = self._allocate_xid()
        credential = self.credential
        if self.randomise_stamp and isinstance(credential, rpc.AuthUnix):
            credential = replace(credential, stamp=rpc.random_stamp())
        datagram = rpc.build_call(
            xid, program, version, procedure, args, credential
        )
        pending = _PendingCall(
            xid=xid,
            port=port,
            datagram=datagram,
            handler=handler,
            deadline=self.loop.now() + self.timeout,
            label=label or f"prog={program} proc={procedure}",
        )
        self._pending[xid] = pending
        self.stats["calls"] += 1
        self.channel.sendto(datagram, (self.peer_ip, port))
        return xid

    def call(
        self,
        program: int,
        version: int,
        procedure: int,
        port: int,
        args: bytes = b"",
        label: str = "",
    ) -> rpc.RpcReply:
        """Send a call and drive the loop until it is answered.

        Raises :class:`RpcTimeout` if every retry went unanswered, or
        :class:`~prolinks_poc.proto.rpc.RpcCallFailed` if the server answered
        with an error. A timeout and a rejection are very different findings --
        "nothing is listening" versus "something is listening and said no" --
        so they are never collapsed into one failure mode.
        """
        result: dict = {}

        def on_reply(reply, error) -> None:
            result["reply"] = reply
            result["error"] = error

        self.call_async(program, version, procedure, port, args, on_reply, label)
        # Bound the wait by the worst case the retry schedule allows, plus a
        # tick of slack, so a wedged loop cannot hang the CLI indefinitely.
        budget = self.timeout * (self.retries + 1) + RETRY_TICK_S
        self.loop.run_until(
            predicate=lambda: bool(result), deadline=self.loop.now() + budget
        )

        if not result:
            raise RpcTimeout(
                f"no reply from {self.peer_ip}:{port} for {label or 'RPC call'} "
                f"within {budget:.1f}s"
            )
        if result.get("error") is not None:
            raise result["error"]
        return result["reply"].raise_for_status(label)

    # -- receiving -------------------------------------------------------

    def _on_readable(self) -> None:
        self.channel.drain(self._on_datagram)

    def _on_datagram(self, data: bytes, peer: tuple[str, int]) -> None:
        try:
            reply = rpc.parse_reply(data)
        except DecodeError as exc:
            log.debug("undecodable RPC reply from %s: %s", peer[0], exc)
            self.stats["unmatched"] += 1
            return

        pending = self._pending.pop(reply.xid, None)
        if pending is None:
            # A duplicate answer to a call we already retired: the original
            # reply and its retry both arrived. Expected, not an error.
            self.stats["unmatched"] += 1
            log.debug("reply for unknown xid %#x from %s", reply.xid, peer[0])
            return

        self.stats["replies"] += 1
        if pending.handler is not None:
            pending.handler(reply, None)

    def _on_retry_tick(self) -> None:
        now = self.loop.now()
        for xid, pending in list(self._pending.items()):
            if pending.deadline > now:
                continue
            if pending.attempts > self.retries:
                del self._pending[xid]
                self.stats["timeouts"] += 1
                if pending.handler is not None:
                    pending.handler(
                        None,
                        RpcTimeout(
                            f"{pending.label}: no reply after {pending.attempts} "
                            f"attempts to {self.peer_ip}:{pending.port}"
                        ),
                    )
                continue
            pending.attempts += 1
            pending.deadline = now + self.timeout
            self.stats["retries"] += 1
            log.debug(
                "retry %d/%d for %s (xid %#x)",
                pending.attempts - 1, self.retries, pending.label, xid,
            )
            self.channel.sendto(pending.datagram, (self.peer_ip, pending.port))

    # -- lifecycle -------------------------------------------------------

    @property
    def in_flight(self) -> int:
        return len(self._pending)

    def cancel(self, xid: int) -> None:
        self._pending.pop(xid, None)

    def cancel_all(self) -> None:
        self._pending.clear()

    def close(self) -> None:
        self._retry_timer.cancel()
        self.loop.remove_reader(self.channel.sock)
        self.channel.close()
        self._pending.clear()
