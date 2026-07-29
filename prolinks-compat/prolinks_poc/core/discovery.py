"""Passive discovery: learn the network by listening, and only listening.

This transmits nothing. It binds UDP 50000, decodes what arrives, and folds it
into a :class:`~prolinks_poc.core.devices.DeviceTable`. That is enough to learn
every peer's number, name, MAC and IP -- which in turn is everything the NFS
path needs, *if* experiment E1 confirms a CDJ will serve files to a host that
never announced itself.

The active counterpart lives in :mod:`prolinks_poc.core.announcer` and is only
ever constructed by ``prolinks announce``, so passivity is structural rather
than a flag anyone can forget to set.
"""

from __future__ import annotations

import logging
from typing import Callable

from ..net.loop import EventLoop
from ..net.udp import UdpChannel, djl_socket
from ..proto import djl
from ..proto.errors import DecodeError
from .devices import DeviceEvent, DeviceTable

log = logging.getLogger(__name__)

__all__ = ["PassiveDiscovery", "summarise_packet"]


def summarise_packet(data: bytes) -> dict | None:
    """Journal-friendly summary of a UDP-50000 datagram.

    Returns ``None`` for non-DJ-Link traffic so the journal records the raw
    hex without a misleading decode. Field names match the packet dataclasses,
    so a golden decode and a journal entry can be diffed directly.
    """
    if not djl.is_djl_packet(data):
        return None
    packet = djl.decode(data)
    summary: dict = {
        "packet": type(packet).__name__,
        "type": f"0x{packet.packet_type:02x}" if packet.packet_type is not None else None,
        "name": packet.name,
        "name_raw_hex": packet.name_raw.hex(),
        "device_kind": f"0x{packet.device_kind:02x}",
        "subtype": f"0x{packet.subtype:02x}",
        "stype": f"0x{packet.stype:02x}",
        "wire_length": packet.wire_length,
    }
    for attribute in (
        "device_number",
        "iteration",
        "peer_count",
        "assignment_mode",
        "payload",
        "flags",
        "const_25",
        "trailing",
        "raw_type",
        "ip",
    ):
        if hasattr(packet, attribute):
            summary[attribute] = getattr(packet, attribute)
    if hasattr(packet, "mac"):
        summary["mac"] = djl.format_mac(packet.mac)
    return summary


class PassiveDiscovery:
    """Listens on UDP 50000 and maintains a :class:`DeviceTable`."""

    def __init__(
        self,
        loop: EventLoop,
        table: DeviceTable | None = None,
        recorder=None,
        guard=None,
        bind_ip: str = "0.0.0.0",
        via_interface: str | None = None,
        on_event: Callable[[DeviceEvent], None] | None = None,
    ) -> None:
        self.loop = loop
        self.table = table if table is not None else DeviceTable()
        self.on_event = on_event
        #: Set by :class:`~prolinks_poc.core.announcer.VirtualCdj` while it is
        #: claiming a number, so it can back off. Conflict packets are
        #: unicast to our port 50000, which is why the announcer shares this
        #: object's socket rather than opening its own (research/02 §5).
        self.on_conflict = None
        #: Likewise, so an active announcer can *defend* its own number
        #: against a newcomer proposing it.
        self.on_claim = None
        self.via_interface = via_interface
        self.decode_failures = 0
        self.non_djl = 0

        self.channel = UdpChannel(
            djl_socket(djl.DISCOVERY_PORT, bind_ip, interface=via_interface),
            recorder=recorder,
            guard=guard,
            label="djl:50000",
            decoder=summarise_packet,
        )

    def start(self) -> None:
        self.loop.add_reader(self.channel.sock, self._on_readable)
        # Reap on a 1 s tick: fine-grained enough that a 10 s staleness
        # threshold is accurate to within a tick, cheap enough to ignore.
        self.loop.call_every(1.0, self._on_reap)

    def _on_readable(self) -> None:
        self.channel.drain(self._on_datagram)

    def _on_datagram(self, data: bytes, peer: tuple[str, int]) -> None:
        if not djl.is_djl_packet(data):
            self.non_djl += 1
            return
        try:
            packet = djl.decode(data)
        except DecodeError as exc:
            # Never fatal: an unparseable datagram is a research finding, and
            # the raw bytes are already in the journal for later study.
            self.decode_failures += 1
            log.debug("undecodable 50000 datagram from %s: %s", peer[0], exc)
            return

        if isinstance(packet, djl.NumberConflict) and self.on_conflict is not None:
            self.on_conflict(packet, peer[0])
        elif (
            isinstance(packet, (djl.ClaimIp, djl.ClaimNumber))
            and packet.device_number
            and self.on_claim is not None
        ):
            self.on_claim(packet.device_number, peer[0])

        event = self.table.observe(
            packet, peer[0], now=self.loop.now(), via_interface=self.via_interface
        )
        if event is not None and self.on_event is not None:
            self.on_event(event)

    def _on_reap(self) -> None:
        for event in self.table.reap(now=self.loop.now()):
            if self.on_event is not None:
                self.on_event(event)

    def close(self) -> None:
        self.loop.remove_reader(self.channel.sock)
        self.channel.close()

    def __enter__(self) -> "PassiveDiscovery":
        return self

    def __exit__(self, *exc) -> None:
        self.close()
