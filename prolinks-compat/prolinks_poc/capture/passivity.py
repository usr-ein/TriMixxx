"""Enforcement for experiment E1 — "does passive NFS access work?".

E1 asks whether a CDJ will serve us files over NFS when we have never
announced ourselves on the DJ-Link ports. Answering it credibly requires
*proving* we stayed silent, and a post-hoc grep of a pcap is weak evidence: it
cannot distinguish "we sent nothing" from "we sent something the filter
missed".

So passivity is enforced two ways:

1. **Structurally.** Only ``prolinks announce`` constructs a transmitter on a
   DJ-Link port at all. Passivity is a property of the command set.
2. **Actively,** by this guard. When armed it raises before the ``sendto``
   syscall, so a violation is a loud traceback rather than a silent stray
   datagram. Belt and braces, because the whole experiment turns on it.

The journal written by :mod:`prolinks_poc.capture.recorder` then provides the
independent, after-the-fact record for the write-up.
"""

from __future__ import annotations

from ..proto.djl import BEAT_PORT, DISCOVERY_PORT, STATUS_PORT

__all__ = ["PassivityViolation", "TransmitGuard", "DJ_LINK_PORTS"]

#: The four ports a device must never touch to count as passive. 50004 is the
#: "touch audio" port (research/01 §3); we never use it, but a guard that only
#: covers the ports we happen to know about proves less.
DJ_LINK_PORTS = frozenset({DISCOVERY_PORT, BEAT_PORT, STATUS_PORT, 50004})


class PassivityViolation(RuntimeError):
    """Raised when armed and something tried to transmit on a DJ-Link port."""


class TransmitGuard:
    """Blocks transmission on the DJ-Link ports while armed."""

    __slots__ = ("armed", "ports", "_allowed")

    def __init__(self, armed: bool = False, ports: frozenset[int] = DJ_LINK_PORTS) -> None:
        self.armed = armed
        self.ports = ports
        self._allowed = False

    def check(self, local_port: int, peer: tuple[str, int]) -> None:
        """Raise if this send would break passivity."""
        if not self.armed or self._allowed:
            return
        peer_port = peer[1] if peer else 0
        if local_port in self.ports or peer_port in self.ports:
            raise PassivityViolation(
                f"passivity is armed but something tried to send from port "
                f"{local_port} to {peer[0]}:{peer_port}. "
                f"DJ-Link ports are {sorted(self.ports)}."
            )

    def allow(self) -> "_AllowScope":
        """Temporarily permit transmission (used by ``announce --dry-run`` tests)."""
        return _AllowScope(self)


class _AllowScope:
    __slots__ = ("_guard", "_previous")

    def __init__(self, guard: TransmitGuard) -> None:
        self._guard = guard
        self._previous = guard._allowed

    def __enter__(self) -> None:
        self._guard._allowed = True

    def __exit__(self, *exc) -> None:
        self._guard._allowed = self._previous
