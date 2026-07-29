"""The peer table: who is on the DJ-Link network right now.

Devices are keyed by **MAC address**, not by device number or IP. Both of the
obvious alternatives are unstable in ways that matter here: a player's number
can be reassigned during the startup handshake, and its IP changes if the
network switches between DHCP and link-local self-assignment. The MAC is the
one identifier that survives both, and it is what the Mixxx feature will use
for its cache and sidebar keys too.

A two-tier lifetime is modelled rather than a single timeout. A CDJ can drop
off for a couple of seconds -- a nudged cable, a switch reconverging -- and
come straight back. Treating that as "device removed" would, in Mixxx, tear
down the sidebar subtree and the parsed library for a blip. So a device that
stops sending keep-alives first goes **stale** (still listed, marked offline,
all state retained) and only later is **forgotten**.
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from typing import Iterable

from ..proto import djl

__all__ = ["Device", "DeviceTable", "DeviceEvent"]

#: A device that has not been heard from for this long is marked offline.
STALE_AFTER_S = djl.DEVICE_TIMEOUT_S
#: ...and only dropped entirely this long after *that*.
FORGET_AFTER_S = 60.0


@dataclass
class Device:
    """One peer, as learned purely by listening."""

    mac: bytes
    ip: str
    name: str
    #: The literal 20 bytes of the name field. Kept because milestone M1 exists
    #: partly to settle the exact casing of ``CDJ-2000nexus``, which every
    #: source infers and none has captured.
    name_raw: bytes
    device_number: int
    device_kind: int
    first_seen: float
    last_seen: float
    peer_count: int = 0
    #: Keep-alive byte 0x35, a product-generation code. 0x00 on all nexus
    #: gear we have captured, 0x64 for CDJ-3000. See :attr:`model_hint`.
    trailing: int = 0x00
    packet_count: int = 0
    stale: bool = False
    #: Which of our interfaces the datagrams arrived on, so RPC sockets can
    #: bind the matching source address on a multi-homed host.
    via_interface: str | None = None

    @property
    def key(self) -> str:
        return self.mac.hex()

    @property
    def mac_str(self) -> str:
        return djl.format_mac(self.mac)

    @property
    def kind_name(self) -> str:
        try:
            return djl.DeviceKind(self.device_kind).name
        except ValueError:
            return f"0x{self.device_kind:02x}"

    @property
    def model_hint(self) -> str:
        """Product generation, from keep-alive byte ``0x35``.

        ``research/02`` §2 reads this byte as "00 classic, 01 typical CDJ,
        64 CDJ-3000", which would make every deck we own "classic". It is
        wrong: both a CDJ-2000nexus and a DJM-2000nexus send **0x00**
        (FINDINGS C3), and ``0x01`` has never been observed on hardware at
        all. So 0x00 is the nexus-and-earlier value, and the label says so.
        """
        return {
            0x00: "nexus/earlier",
            0x01: "0x01 (documented, never observed)",
            0x20: "Stagehand",
            0x64: "CDJ-3000",
        }.get(self.trailing, f"0x{self.trailing:02x}")

    def age(self, now: float | None = None) -> float:
        return (now if now is not None else time.monotonic()) - self.last_seen

    def label(self) -> str:
        return f"{self.device_number} · {self.name}"

    def __str__(self) -> str:
        suffix = "  (offline)" if self.stale else ""
        return (
            f"{self.device_number:>3}  {self.name:<20}  {self.ip:<15}  "
            f"{self.mac_str}  {self.kind_name:<20} {self.model_hint}{suffix}"
        )


@dataclass
class DeviceEvent:
    """Something changed in the table. Consumed by the CLI and, later, by the
    Qt signal layer -- the event kinds map 1:1 onto ``ProLinkDiscovery``'s
    ``deviceFound`` / ``deviceUpdated`` / ``deviceStale`` / ``deviceRevived``.
    """

    kind: str  # found | updated | stale | revived | forgotten
    device: Device


class DeviceTable:
    """Peers keyed by MAC, with staleness and forgetting."""

    def __init__(
        self,
        stale_after: float = STALE_AFTER_S,
        forget_after: float = FORGET_AFTER_S,
    ) -> None:
        self.stale_after = stale_after
        self.forget_after = forget_after
        self._devices: dict[str, Device] = {}
        #: Every device number seen in any keep-alive or claim packet, ever.
        #:
        #: Kept separately from the live table and never pruned, because it
        #: feeds the safe-claim algorithm (research/02 §3.4). XDJ-XZ and Opus
        #: Quad do not defend their numbers with conflict packets, so "I have
        #: not seen a conflict" is not evidence a number is free -- only "I
        #: have never seen anyone use it" is, and that requires remembering
        #: numbers belonging to devices that have since gone quiet.
        self.numbers_seen: set[int] = set()

    # -- ingest ----------------------------------------------------------

    def observe(
        self,
        packet: djl.DjlPacket,
        peer_ip: str,
        now: float | None = None,
        via_interface: str | None = None,
    ) -> DeviceEvent | None:
        """Fold one decoded UDP-50000 packet into the table.

        Only keep-alives create devices: they are the only packet type
        carrying the full set (number, name, MAC, IP) and the only one a
        settled device keeps sending. Claim packets contribute their proposed
        number to :attr:`numbers_seen` but do not create entries, since a
        device mid-handshake may never actually end up with the number it is
        proposing.
        """
        now = time.monotonic() if now is None else now

        if isinstance(packet, (djl.ClaimIp, djl.ClaimNumber, djl.NumberConflict)):
            if packet.device_number:
                self.numbers_seen.add(packet.device_number)
            return None

        if not isinstance(packet, djl.KeepAlive):
            return None

        self.numbers_seen.add(packet.device_number)
        existing = self._devices.get(packet.mac.hex())

        if existing is None:
            device = Device(
                mac=packet.mac,
                ip=packet.ip,
                name=packet.name,
                name_raw=packet.name_raw,
                device_number=packet.device_number,
                device_kind=packet.device_kind,
                first_seen=now,
                last_seen=now,
                peer_count=packet.peer_count,
                trailing=packet.trailing,
                packet_count=1,
                via_interface=via_interface,
            )
            self._devices[device.key] = device
            return DeviceEvent("found", device)

        was_stale = existing.stale
        changed = (
            existing.device_number != packet.device_number
            or existing.ip != packet.ip
            or existing.name != packet.name
        )
        existing.last_seen = now
        existing.packet_count += 1
        existing.device_number = packet.device_number
        existing.ip = packet.ip
        existing.name = packet.name
        existing.name_raw = packet.name_raw
        existing.peer_count = packet.peer_count
        existing.trailing = packet.trailing
        existing.stale = False
        if via_interface:
            existing.via_interface = via_interface

        if was_stale:
            return DeviceEvent("revived", existing)
        if changed:
            return DeviceEvent("updated", existing)
        return None

    # -- lifetime --------------------------------------------------------

    def reap(self, now: float | None = None) -> list[DeviceEvent]:
        """Mark newly-silent devices stale and drop long-gone ones."""
        now = time.monotonic() if now is None else now
        events: list[DeviceEvent] = []
        for key, device in list(self._devices.items()):
            age = now - device.last_seen
            if age > self.stale_after + self.forget_after:
                del self._devices[key]
                events.append(DeviceEvent("forgotten", device))
            elif age > self.stale_after and not device.stale:
                device.stale = True
                events.append(DeviceEvent("stale", device))
        return events

    def forget_stale(self) -> list[DeviceEvent]:
        """Drop every offline device now, without waiting out the grace period.

        Backs an explicit user refresh -- the equivalent of Mixxx's
        ``[ProLink],refresh`` control.
        """
        events = []
        for key, device in list(self._devices.items()):
            if device.stale:
                del self._devices[key]
                events.append(DeviceEvent("forgotten", device))
        return events

    # -- queries ---------------------------------------------------------

    def all(self, include_stale: bool = True) -> list[Device]:
        devices = self._devices.values()
        if not include_stale:
            devices = [d for d in devices if not d.stale]
        return sorted(devices, key=lambda d: (d.device_number, d.ip))

    def by_number(self, number: int) -> Device | None:
        for device in self.all():
            if device.device_number == number:
                return device
        return None

    def by_ip(self, ip: str) -> Device | None:
        for device in self.all():
            if device.ip == ip:
                return device
        return None

    def free_numbers(self, candidates: Iterable[int]) -> list[int]:
        """Candidate numbers never observed in use. See :attr:`numbers_seen`."""
        return [n for n in candidates if n not in self.numbers_seen]

    def __len__(self) -> int:
        return len(self._devices)

    def __iter__(self):
        return iter(self.all())
