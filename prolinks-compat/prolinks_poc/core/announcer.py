"""The virtual CDJ: announce ourselves, claim a device number, defend it.

This is the **only** part of the PoC that transmits on a DJ-Link port, which is
what makes every other command passive by construction (see
:mod:`prolinks_poc.capture.passivity`).

Two escalating modes, because they carry very different risk:

``announce`` (no claim)
    Emit keep-alives at a number outside 1-6 and never contend for anything.
    This is what the reference tools do. Enough to be *seen*, and enough for
    peers to start unicasting status to us -- but not enough to issue dbserver
    metadata queries, which need a number of 6 or lower (``research/02`` §3.2).

``announce --claim``
    Run the full handshake and take a real player slot. Necessary for dbserver,
    and the only mode that can disturb a live rig, so it is opt-in, it refuses
    a number it has seen anyone else use, and it backs off on conflict.

The whole thing is an explicit ``enum`` state machine rather than a coroutine,
so it transcribes directly into ``ProLinkVirtualCdj`` in Mixxx.

The handshake (``research/02`` §1.0), all broadcast, ~300 ms apart::

    3x HELLO(0a) -> 3x CLAIM_MAC(00) -> 3x CLAIM_IP(02) -> 3x CLAIM_NUMBER(04)
    -> KEEP_ALIVE(06) every 1.5 s forever

A device already holding our proposed number unicasts a ``NUMBER_CONFLICT``
(``08``) back to our port 50000, on receipt of which we abandon it and try the
next candidate.
"""

from __future__ import annotations

import enum
import logging
from typing import Callable

from ..net.iface import Interface
from ..net.loop import EventLoop
from ..net.udp import UdpChannel, rpc_socket
from ..proto import djl
from ..proto import djl_status as status

log = logging.getLogger(__name__)

__all__ = [
    "AnnouncerState",
    "VirtualCdj",
    "SAFE_OBSERVER_NUMBER",
    "PLAYER_NUMBERS",
    "STATUS_INTERVAL_S",
]

#: Outside the 1-6 player range, so it can never collide with real hardware.
#: Cannot issue dbserver queries (``research/02`` §3.2) -- the price of safety.
SAFE_OBSERVER_NUMBER = 7
#: Numbers that are real deck slots, in the order we would try to claim them.
#: Descending, so we take the highest free slot and leave 1 and 2 -- the decks
#: a DJ reaches for first -- alone as long as possible.
PLAYER_NUMBERS = (4, 3, 2, 1)

#: How long to listen before claiming. research/02 §3.4 makes this mandatory:
#: XDJ-XZ and Opus Quad do not defend their numbers with conflict packets, so
#: silence is not evidence a number is free -- only having watched is.
PRESCAN_SECONDS = 2.5


class AnnouncerState(enum.Enum):
    IDLE = "idle"
    PRESCAN = "prescan"
    HELLO = "hello"
    CLAIM_MAC = "claim_mac"
    CLAIM_IP = "claim_ip"
    CLAIM_NUMBER = "claim_number"
    ACTIVE = "active"
    FAILED = "failed"


#: Status-packet cadence. Measured at 199 ms mean (min 63, max 207) on a real
#: CDJ-2000nexus, matching research/03's "roughly every 200 ms".
STATUS_INTERVAL_S = 0.2

#: Keep-alive byte ``25``, latched at boot: ``02`` if we were the first device
#: on the network, ``01`` if peers were already present. See FINDINGS F9 --
#: six observed boots, no exceptions.
BYTE25_FIRST_ON_NETWORK = 0x02
BYTE25_JOINED_PEERS = 0x01


def _handshake_stages(first_on_network: bool):
    """The handshake stages in order, with how many packets each sends.

    The stage-3 repeat count is **not** governed by the auto/manual assignment
    mode, as ``research/02`` §1.0 has it, but by whether anyone else was on the
    network at boot: three when first, one when joining (FINDINGS C13).
    """
    return (
        (AnnouncerState.HELLO, 3),
        (AnnouncerState.CLAIM_MAC, 3),
        (AnnouncerState.CLAIM_IP, 3),
        (AnnouncerState.CLAIM_NUMBER, 3 if first_on_network else 1),
    )


class VirtualCdj:
    """Announces us on the network, optionally claiming a real player number.

    Shares the discovery object's socket rather than opening its own: replies
    to our claim -- conflicts, and mixer assignments -- are **unicast to port
    50000**, so we must send *from* the port we are listening on
    (``research/02`` §5). Two sockets would mean never hearing the answer.
    """

    def __init__(
        self,
        loop: EventLoop,
        discovery,
        interface: Interface,
        device_number: int = SAFE_OBSERVER_NUMBER,
        name: str = "CDJ-2000nexus",
        device_kind: int = djl.DeviceKind.CDJ,
        claim: bool = False,
        dry_run: bool = False,
        trailing: int = 0x00,
        emit_status: bool = False,
        has_usb: bool = False,
        recorder=None,
        on_state: Callable[[AnnouncerState, str], None] | None = None,
    ) -> None:
        self.loop = loop
        self.discovery = discovery
        self.interface = interface
        self.name = name
        self.device_kind = device_kind
        self.claim = claim
        self.dry_run = dry_run
        #: research/02 §1.6 / FINDINGS C3: 00 is what real nexus gear sends;
        #: 64 is required to coexist with CDJ-3000s on players 5/6.
        self.trailing = trailing
        self.on_state = on_state
        #: Emit CDJ status packets on 50002. Without these a player sees us as
        #: a device with empty slots, because media presence is advertised
        #: there and nowhere else (FINDINGS F20/F21).
        self.emit_status = emit_status
        self.has_usb = has_usb
        self.recorder = recorder
        self._status_channel: UdpChannel | None = None
        self._status_timer = None
        self._status_counter = 0

        self.state = AnnouncerState.IDLE
        self.device_number = device_number
        self.sent: list[bytes] = []  # dry-run output
        self.conflicts: list[int] = []
        #: Latched in :meth:`start`. Real decks decide this once at boot and
        #: hold it for the whole session -- byte 0x25 never changed in any of
        #: the ten device-sessions captured -- so it must not be recomputed as
        #: peers come and go.
        self.first_on_network: bool | None = None
        self._handshake = _handshake_stages(True)

        self._stage = 0
        self._iteration = 0
        self._candidates: list[int] = []
        self._timer = None
        self._keepalive_timer = None

    # -- lifecycle -------------------------------------------------------

    def start(self) -> None:
        """Begin announcing. Returns immediately; drive the loop to progress."""
        self.discovery.on_conflict = self._on_conflict
        # Latch "was I first here?" now and never revisit it: a real deck
        # decides this at boot, and it drives both the stage-3 repeat count
        # and keep-alive byte 0x25 for the rest of the session.
        self.first_on_network = len(self.discovery.table) == 0
        self._handshake = _handshake_stages(self.first_on_network)
        log.info(
            "%s on the network (%d peer(s) already present)",
            "first" if self.first_on_network else "joining",
            len(self.discovery.table),
        )
        if not self.claim:
            # No contention: just start emitting keep-alives at our number.
            self._enter(AnnouncerState.ACTIVE, f"announcing as device {self.device_number}")
            self._start_keepalive()
            return

        self._candidates = [
            n for n in PLAYER_NUMBERS if n != self.device_number
        ]
        self._enter(
            AnnouncerState.PRESCAN,
            f"watching for {PRESCAN_SECONDS:.1f}s before claiming (research/02 §3.4)",
        )
        self._timer = self.loop.call_later(PRESCAN_SECONDS, self._finish_prescan)

    def stop(self) -> None:
        for timer in (self._timer, self._keepalive_timer, self._status_timer):
            if timer is not None:
                timer.cancel()
        self._timer = self._keepalive_timer = self._status_timer = None
        if self._status_channel is not None:
            self._status_channel.close()
            self._status_channel = None
        self.discovery.on_conflict = None
        self._enter(AnnouncerState.IDLE, "stopped")

    # -- claiming --------------------------------------------------------

    def _finish_prescan(self) -> None:
        """Pick a number nobody has been seen using, then run the handshake."""
        table = self.discovery.table
        if self.device_number in table.numbers_seen:
            free = [n for n in PLAYER_NUMBERS if n not in table.numbers_seen]
            if not free:
                self._enter(
                    AnnouncerState.FAILED,
                    f"device {self.device_number} is taken and players "
                    f"{sorted(table.numbers_seen)} are all in use -- nothing free to claim",
                )
                return
            log.info("device %d is taken; switching to %d", self.device_number, free[0])
            self.device_number = free[0]

        self._stage = 0
        self._iteration = 0
        self._advance()

    def _advance(self) -> None:
        """Send the next handshake packet, or transition to keep-alive."""
        if self._stage >= len(self._handshake):
            self._enter(
                AnnouncerState.ACTIVE, f"claimed device number {self.device_number}"
            )
            self._start_keepalive()
            return

        state, count = self._handshake[self._stage]
        if state is not self.state:
            self._enter(state, f"{state.value} 1/{count}")
        self._iteration += 1
        self._send(self._build_handshake_packet(state, self._iteration))

        if self._iteration >= count:
            self._stage += 1
            self._iteration = 0
        self._timer = self.loop.call_later(djl.DISCOVERY_INTERVAL_S, self._advance)

    def _build_handshake_packet(self, state: AnnouncerState, iteration: int):
        common = dict(
            name=self.name, name_raw=b"", device_kind=self.device_kind
        )
        if state is AnnouncerState.HELLO:
            return djl.Hello(
                **common, payload=0x02 if self.device_kind == djl.DeviceKind.MIXER else 0x01
            )
        if state is AnnouncerState.CLAIM_MAC:
            return djl.ClaimMac(
                **common,
                iteration=iteration,
                flags=djl.default_role(self.device_kind),
                mac=self.interface.mac,
            )
        if state is AnnouncerState.CLAIM_IP:
            return djl.ClaimIp(
                **common,
                ip=self.interface.ip,
                mac=self.interface.mac,
                device_number=self.device_number,
                iteration=iteration,
                assignment_mode=djl.AssignmentMode.MANUAL,
            )
        return djl.ClaimNumber(
            **common, device_number=self.device_number, iteration=iteration
        )

    def _on_conflict(self, packet: djl.NumberConflict, peer_ip: str) -> None:
        """Someone is defending the number we are claiming: give it up."""
        if packet.device_number != self.device_number:
            return
        self.conflicts.append(packet.device_number)
        log.warning(
            "device %d is claimed by %s -- backing off",
            packet.device_number, packet.ip or peer_ip,
        )
        self.discovery.table.numbers_seen.add(packet.device_number)

        if self._timer is not None:
            self._timer.cancel()
        remaining = [n for n in self._candidates if n not in self.discovery.table.numbers_seen]
        if not remaining:
            self._enter(
                AnnouncerState.FAILED,
                f"device {self.device_number} is taken and no candidate remains",
            )
            return
        self.device_number = remaining[0]
        self._candidates = remaining[1:]
        self._enter(AnnouncerState.CLAIM_MAC, f"retrying with device {self.device_number}")
        self._stage = 1  # back to CLAIM_MAC; the hellos do not need repeating
        self._iteration = 0
        self._advance()

    def defend(self, proposed_number: int, challenger_ip: str) -> None:
        """Emit a conflict packet when someone proposes a number we hold.

        Required to be a well-behaved peer (``research/02`` §1.5): a device
        that takes a number but never defends it will simply lose it to the
        next player that boots.
        """
        if self.state is not AnnouncerState.ACTIVE or proposed_number != self.device_number:
            return
        log.info("defending device %d against %s", self.device_number, challenger_ip)
        packet = djl.NumberConflict(
            name=self.name, name_raw=b"", device_kind=self.device_kind,
            device_number=self.device_number, ip=self.interface.ip,
        )
        self._send(packet, unicast_to=challenger_ip)

    # -- keep-alive ------------------------------------------------------

    def _start_keepalive(self) -> None:
        self._send_keepalive()
        self._keepalive_timer = self.loop.call_every(
            djl.KEEPALIVE_INTERVAL_S, self._send_keepalive
        )
        if self.emit_status:
            self._start_status()

    # -- status (announced mode only) ------------------------------------

    def _start_status(self) -> None:
        """Begin emitting status packets to each known peer.

        Status is **unicast** on real hardware -- not one of 1507 captured
        packets was broadcast (FINDINGS F21) -- so this sends one copy per
        peer rather than a single broadcast. Peers that have not announced
        themselves get nothing, which mirrors why we received nothing until
        we announced.
        """
        if self.dry_run:
            log.info("DRY RUN: would emit status every %.0f ms", STATUS_INTERVAL_S * 1000)
            return
        self._status_channel = UdpChannel(
            rpc_socket(self.interface.ip, 0),
            recorder=self.recorder,
            guard=None,  # announced mode: transmitting is the whole point
            label="status:50002",
        )
        self._status_timer = self.loop.call_every(STATUS_INTERVAL_S, self._send_status)

    def build_status(self) -> bytes:
        """The status packet we emit. Exposed for byte-diffing against real ones."""
        return status.build_status(
            device_number=self.device_number,
            name=self.name,
            usb_state=(
                status.MediaState.LOADED if self.has_usb else status.MediaState.EMPTY
            ),
            sd_state=status.MediaState.EMPTY,
            link_available=1 if (self.has_usb or self.discovery.table) else 0,
            packet_counter=self._status_counter,
        )

    def _send_status(self) -> None:
        if self._status_channel is None:
            return
        packet = self.build_status()
        self._status_counter = (self._status_counter + 1) & 0xFFFFFFFF
        for device in self.discovery.table.all(include_stale=False):
            try:
                self._status_channel.sendto(packet, (device.ip, status.STATUS_PORT))
            except OSError as exc:
                log.debug("status to %s failed: %s", device.ip, exc)

    def _send_keepalive(self) -> None:
        self._send(self.build_keepalive())

    def build_keepalive(self) -> djl.KeepAlive:
        """The packet we broadcast every 1.5 s. Exposed for byte-diffing.

        Milestone M9's acceptance test is to diff this against a real
        CDJ-2000nexus keep-alive from the capture journal and justify every
        differing byte -- so it needs to be inspectable without sending.
        """
        return djl.KeepAlive(
            name=self.name,
            name_raw=b"",
            device_kind=self.device_kind,
            device_number=self.device_number,
            mac=self.interface.mac,
            ip=self.interface.ip,
            # Peer count includes ourselves (research/02 §2).
            peer_count=len(self.discovery.table) + 1,
            # Latched at boot; see FINDINGS F9.
            const_25=(
                BYTE25_FIRST_ON_NETWORK
                if self.first_on_network is not False
                else BYTE25_JOINED_PEERS
            ),
            trailing=self.trailing,
        )

    # -- transmission ----------------------------------------------------

    def _send(self, packet, unicast_to: str | None = None) -> None:
        raw = packet.encode()
        destination = (
            unicast_to if unicast_to is not None else self.interface.broadcast,
            djl.DISCOVERY_PORT,
        )
        if self.dry_run:
            self.sent.append(raw)
            log.info("DRY RUN would send %s to %s: %s",
                     type(packet).__name__, destination[0], raw.hex())
            return
        self.discovery.channel.sendto(raw, destination)

    def _enter(self, state: AnnouncerState, message: str) -> None:
        self.state = state
        log.info("[%s] %s", state.value, message)
        if self.on_state is not None:
            self.on_state(state, message)
