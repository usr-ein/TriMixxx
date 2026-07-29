"""Command-line surface for the PoC.

The only module allowed to use anything outside the standard library (it does
not currently need to). Commands are grouped by milestone; see
``research/10-mixxx-prolink-implementation-plan.md``.

**Passivity is structural.** ``announce`` is the only command that will ever
transmit on a DJ-Link port, so every other command is passive by construction
rather than by remembering to pass a flag. ``--assert-passive`` additionally
arms a guard that raises before the ``sendto`` syscall, and verifies the
capture journal afterwards. Experiment E1 turns on that claim, so it is worth
enforcing twice.
"""

from __future__ import annotations

import argparse
import ipaddress
import json
import logging
import shlex
from collections import Counter
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

from .capture.passivity import DJ_LINK_PORTS, TransmitGuard
from .capture.pcap import read_capture, read_dump_file
from .capture.recorder import Recorder
from .core.announcer import PLAYER_NUMBERS, SAFE_OBSERVER_NUMBER, AnnouncerState, VirtualCdj
from .core.devices import Device, DeviceTable
from .core.discovery import PassiveDiscovery
from .core.library import Library
from .core.slots import PDB_PATH, MediaSlot, export_path_for, slot_from_name
from .net.iface import (
    find_interface,
    interface_for_peer,
    list_interfaces,
    warn_if_route_mismatched,
)
from .net.loop import EventLoop
from .net.dbclient import DbClient, DbServerUnavailable, discover_port
from .net.dbserverd import DbServer
from .net.nfsclient import DEFAULT_CHUNK, DEFAULT_WINDOW, DownloadStats, NfsClient
from .net.nfsserver import NfsServer
from .net.vfs import Vfs
from .net.rpcclient import RpcTimeout
from .net.udp import UdpChannel, djl_socket
from .proto import dbserver as dbproto
from .proto import djl, mountd, nfs2, portmap, rpc
from .proto.bytes import hexdump
from .proto.errors import ProlinkError

log = logging.getLogger("prolinks")

DEFAULT_DISCOVERY_WAIT_S = 4.0

#: Commands that touch no network at all. They still get a Recorder for its
#: counters, but creating a capture directory for them would litter
#: captures/ with empty journals every time a file is analysed.
OFFLINE_COMMANDS = frozenset({"pcap", "pdb-dump", "interfaces"})


# -- shared context --------------------------------------------------------


class Context:
    """Everything a command needs: loop, capture journal, guard, interface."""

    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.loop = EventLoop()

        directory = None
        if args.record and args.command not in OFFLINE_COMMANDS:
            directory = Path(args.capture_dir) if args.capture_dir else _default_capture_dir()
        self.recorder = Recorder(directory)
        self.capture_dir = directory

        self.guard = TransmitGuard(armed=args.assert_passive)
        self._interface = None

        self.recorder.note(
            "command",
            argv=shlex.join(sys.argv),
            command=args.command,
            assert_passive=args.assert_passive,
            notes=getattr(args, "notes", None),
        )

    @property
    def interface(self):
        if self._interface is None:
            self._interface = find_interface(self.args.iface)
            # A mismatched link-local route makes every broadcast fail while
            # the interface itself looks fine, so say so up front rather than
            # letting it surface as a wall of 'No route to host'.
            warning = warn_if_route_mismatched(self._interface)
            if warning is not None:
                _warn(f"WARNING: {warning}")
        return self._interface

    def discover(self, seconds: float = DEFAULT_DISCOVERY_WAIT_S) -> DeviceTable:
        """Listen for *seconds* and return what was heard."""
        discovery = PassiveDiscovery(
            self.loop, recorder=self.recorder, guard=self.guard,
            via_interface=self.args.iface,
        )
        discovery.start()
        _warn(f"listening on UDP {djl.DISCOVERY_PORT} for {seconds:.0f}s ...")
        self.loop.run_for(seconds)
        discovery.close()
        return discovery.table

    def resolve(self, spec: str) -> str:
        """Turn a ``<device>`` argument into an IP address.

        A literal IP is used as-is and **skips discovery entirely**, which is
        what makes experiment E1 possible: reaching a player by address proves
        nothing was transmitted to find it.
        """
        try:
            ipaddress.IPv4Address(spec)
            return spec
        except ValueError:
            pass

        try:
            number = int(spec)
        except ValueError:
            raise SystemExit(
                f"cannot interpret device {spec!r}: expected a player number or an IP"
            ) from None

        device = self.discover().by_number(number)
        if device is None:
            raise SystemExit(f"no device with player number {number} was heard on the network")
        _warn(f"player {number} is {device.ip} ({device.name})")
        return device.ip

    def nfs(self, peer_ip: str) -> NfsClient:
        """An NFS client bound to the interface that can actually reach *peer_ip*.

        On a multi-homed host -- a Pi with eth0 on the CDJ network and wlan0
        elsewhere -- binding the wrong source address means every request
        leaves the wrong NIC and times out with no diagnostic at all.
        """
        interface = interface_for_peer(peer_ip)
        local_ip = interface.ip if interface else "0.0.0.0"
        if interface:
            log.debug("reaching %s via %s (%s)", peer_ip, interface.name, local_ip)
        else:
            _warn(
                f"no local interface shares a subnet with {peer_ip}; "
                "binding 0.0.0.0 and hoping the routing table is right"
            )

        credential = _build_credential(self.args)
        return NfsClient(
            self.loop,
            peer_ip,
            local_ip=local_ip,
            recorder=self.recorder,
            guard=self.guard,
            credential=credential,
            timeout=self.args.timeout,
            retries=self.args.retries,
            source_port=getattr(self.args, "source_port", 0),
        )

    def finish(self) -> None:
        """Verify passivity, report where the capture went, and close up."""
        if self.args.assert_passive:
            transmitted = self.recorder.transmitted_on(DJ_LINK_PORTS)
            if transmitted:
                raise SystemExit(
                    f"PASSIVITY VIOLATED: transmitted on DJ-Link ports {transmitted}"
                )
            _warn(
                f"passivity verified: 0 datagrams sent on DJ-Link ports "
                f"{sorted(DJ_LINK_PORTS)} ({self.recorder.tx_total} sent in total, "
                f"all on ephemeral RPC ports)"
            )
        if self.capture_dir is not None:
            _warn(f"capture written to {self.capture_dir}")
        self.recorder.close()
        self.loop.close()


def _build_credential(args: argparse.Namespace):
    """Construct the RPC credential selected by ``--auth`` / ``--stamp``.

    Both flavours are reachable from the command line so that experiment E2 --
    why libcdj's mount failed with ``NFSERR_ACCES`` -- is a matter of running
    the command four ways rather than editing code.
    """
    flavour = getattr(args, "auth", "unix")
    if flavour == "null":
        return rpc.AUTH_NULL_CRED
    stamp = getattr(args, "stamp", None)
    return rpc.AuthUnix(stamp=stamp if stamp is not None else rpc.STAMP_OBSERVED_CDJ)


def _default_capture_dir() -> Path:
    """Where an unnamed run's journal lands.

    Under ``captures/journals/`` rather than ``captures/`` directly, so that the
    hand-named scenario directories -- the ones with a ``NOTES.md`` recording
    what the hardware was doing -- stay legible instead of being buried among
    dozens of timestamps.
    """
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    return Path("captures") / "journals" / stamp


def _warn(message: str) -> None:
    """Progress and diagnostics go to stderr, so ``--json`` stdout stays clean."""
    print(message, file=sys.stderr)


def _emit(args: argparse.Namespace, payload, render) -> None:
    if args.json:
        print(json.dumps(payload, indent=2, sort_keys=True, default=str))
    else:
        render()


# -- M0: sniff -------------------------------------------------------------


def cmd_sniff(ctx: Context) -> int:
    args = ctx.args
    ports = [int(p) for p in args.ports.split(",")]
    channels = []
    counts = {port: 0 for port in ports}

    for port in ports:
        decoder = None
        if args.decode and port == djl.DISCOVERY_PORT:
            from .core.discovery import summarise_packet

            decoder = summarise_packet
        channel = UdpChannel(
            djl_socket(port), recorder=ctx.recorder, guard=ctx.guard,
            label=f"sniff:{port}", decoder=decoder,
        )
        channels.append(channel)

    def make_handler(port: int, channel: UdpChannel):
        def handler(data: bytes, peer) -> None:
            counts[port] += 1
            stamp = time.strftime("%H:%M:%S")
            marker = "" if djl.is_djl_packet(data) else "  [not DJ-Link]"
            print(f"{stamp}  {port}  <- {peer[0]:<15} {len(data):>5}B{marker}")
            if args.decode and djl.is_djl_packet(data):
                try:
                    packet = djl.decode(data)
                    print(f"          {type(packet).__name__}: {packet}")
                except ProlinkError as exc:
                    print(f"          undecodable: {exc}")
            if args.hex:
                print(hexdump(data, indent="          "))

        return handler

    for port, channel in zip(ports, channels):
        ctx.loop.add_reader(channel.sock, _drainer(channel, make_handler(port, channel)))

    _warn(f"sniffing UDP {ports} for {args.duration:.0f}s -- Ctrl-C to stop early")
    try:
        ctx.loop.run_for(args.duration)
    except KeyboardInterrupt:
        _warn("interrupted")

    for channel in channels:
        channel.close()

    _warn("")
    for port in ports:
        _warn(f"  port {port}: {counts[port]} datagrams")
    return 0


def _drainer(channel: UdpChannel, handler):
    def on_readable() -> None:
        channel.drain(handler)

    return on_readable


# -- M1: devices -----------------------------------------------------------


def cmd_devices(ctx: Context) -> int:
    args = ctx.args
    seen: list[str] = []

    def on_event(event) -> None:
        device = event.device
        if event.kind in ("found", "revived"):
            _warn(f"  + {event.kind}: {device.label()} at {device.ip}")
        elif event.kind == "stale":
            _warn(f"  - offline: {device.label()}")
        seen.append(event.kind)

    discovery = PassiveDiscovery(
        ctx.loop, recorder=ctx.recorder, guard=ctx.guard,
        via_interface=ctx.interface.name, on_event=on_event,
    )
    discovery.start()

    duration = args.timeout_s if not args.watch else float("inf")
    _warn(f"listening on UDP {djl.DISCOVERY_PORT}{' (Ctrl-C to stop)' if args.watch else ''} ...")
    try:
        if args.watch:
            ctx.loop.run_until(predicate=lambda: False)
        else:
            ctx.loop.run_for(duration)
    except KeyboardInterrupt:
        _warn("interrupted")

    table = discovery.table
    devices = table.all()

    payload = {
        "devices": [
            {
                "device_number": d.device_number,
                "name": d.name,
                # The literal 20 bytes. Milestone M1 exists partly to settle
                # the exact casing of "CDJ-2000nexus", which every published
                # source infers and none has captured.
                "name_raw_hex": d.name_raw.hex(),
                "ip": d.ip,
                "mac": d.mac_str,
                "device_kind": f"0x{d.device_kind:02x}",
                "kind_name": d.kind_name,
                "keepalive_trailing": f"0x{d.trailing:02x}",
                "model_hint": d.model_hint,
                "peer_count": d.peer_count,
                "packets": d.packet_count,
                "stale": d.stale,
            }
            for d in devices
        ],
        "numbers_seen": sorted(table.numbers_seen),
        "free_player_numbers": table.free_numbers(range(1, 7)),
        "decode_failures": discovery.decode_failures,
        "non_djl_datagrams": discovery.non_djl,
    }

    def render() -> None:
        if not devices:
            print("no devices heard.")
            print()
            print("If two CDJs are powered on and cabled, check:")
            print("  - they share a subnet with this host (UTILITY -> LINK shows their IP)")
            print("  - the capture interface is the one facing them (--iface)")
            print("  - the host firewall permits inbound UDP 50000")
            return
        print(f"{'D':>3}  {'name':<20}  {'ip':<15}  {'mac':<17}  {'kind':<20} model")
        for device in devices:
            print(device)
        print()
        print("literal 20-byte name fields (research/02 §4.1 gap):")
        for device in devices:
            print(f"  {device.name!r:<24} {device.name_raw.hex()}")
        print()
        print(f"device numbers ever seen: {sorted(table.numbers_seen)}")
        print(f"free player numbers 1-6:  {table.free_numbers(range(1, 7))}")

    _emit(args, payload, render)
    discovery.close()
    return 0


def cmd_interfaces(ctx: Context) -> int:
    interfaces = list_interfaces()
    payload = {
        "interfaces": [
            {
                "name": i.name, "ip": i.ip, "netmask": i.netmask,
                "broadcast": i.broadcast, "mac": i.mac_str,
                "link_local": i.is_link_local,
            }
            for i in interfaces
        ]
    }

    def render() -> None:
        for interface in interfaces:
            marker = "  <- link-local, likely the CDJ network" if interface.is_link_local else ""
            print(f"{interface}  bcast={interface.broadcast}{marker}")

    _emit(ctx.args, payload, render)
    return 0


# -- M2: rpcinfo -----------------------------------------------------------


def cmd_rpcinfo(ctx: Context) -> int:
    """The go/no-go gate. Experiment E4: does this player run an RPC stack?"""
    args = ctx.args
    peer = ctx.resolve(args.device)
    client = ctx.nfs(peer)

    result: dict = {"peer": peer, "portmap_responds": False, "mappings": [], "ports": {}}

    _warn(f"probing portmapper at {peer}:{portmap.PORT} ...")
    result["portmap_responds"] = client.ping_portmap()

    if not result["portmap_responds"]:
        def render_dead() -> None:
            print(f"{peer}: no response to portmap NULL on UDP/{portmap.PORT}.")
            print()
            print("This is experiment E4's negative outcome. Before concluding that")
            print("this player has no RPC stack, re-run with media inserted and with")
            print("a track loaded -- research/06 §1's 'confirmed' NFS evidence comes")
            print("from an XDJ capture, not from a CDJ-2000NXS, so the honest prior")
            print("is that we do not know. If every media/playback combination on")
            print("both units is silent here, the NFS transport is dead for this")
            print("hardware and the plan pivots to the dbserver path (research/04).")

        _emit(args, result, render_dead)
        client.close()
        return 1

    try:
        mappings = client.dump_portmap()
        result["mappings"] = [
            {
                "program": m.program, "program_name": m.program_name,
                "version": m.version, "protocol": m.protocol_name, "port": m.port,
            }
            for m in mappings
        ]
    except (ProlinkError, RpcTimeout) as exc:
        mappings = []
        result["dump_error"] = str(exc)
        _warn(f"portmap DUMP failed ({exc}); falling back to targeted GETPORT")

    for label, program, version in (
        ("mountd", mountd.PROGRAM, mountd.VERSION),
        ("nfsd", nfs2.PROGRAM, nfs2.VERSION),
    ):
        try:
            result["ports"][label] = client.get_port(program, version)
        except (ProlinkError, RpcTimeout) as exc:
            result["ports"][label] = 0
            result[f"{label}_error"] = str(exc)

    verdict = bool(result["ports"].get("mountd")) and bool(result["ports"].get("nfsd"))
    result["nfs_available"] = verdict

    def render() -> None:
        print(f"{peer}: portmapper responds on UDP/{portmap.PORT}")
        if mappings:
            print()
            print(f"{'program':>7}  {'v':>2}  {'prot':<4} {'port':>6}  name")
            for mapping in mappings:
                print(mapping)
        print()
        for label in ("mountd", "nfsd"):
            port = result["ports"].get(label, 0)
            print(f"  {label:<8} {'not registered' if not port else f'UDP {port}'}")
        print()
        print(f"VERDICT: NFS transport {'AVAILABLE' if verdict else 'NOT available'} on {peer}")
        if not verdict:
            print("  Portmapper answered but the NFS programs are not registered.")
            print("  Retry with media inserted -- they may register only on mount.")

    _emit(args, result, render)
    client.close()
    return 0 if verdict else 1


# -- M3: exports / mount ---------------------------------------------------


def cmd_exports(ctx: Context) -> int:
    """Experiment E3: what does this player actually export?"""
    args = ctx.args
    peer = ctx.resolve(args.device)
    client = ctx.nfs(peer)
    try:
        exports = client.list_exports()
    except (ProlinkError, RpcTimeout) as exc:
        _warn(f"MOUNT EXPORT failed: {exc}")
        _warn(
            "If EXPORT is unimplemented, fall back to the documented slot table "
            "(SD=/B/, USB=/C/, rekordbox=/) and record that in docs/FINDINGS.md."
        )
        client.close()
        return 1

    payload = {
        "peer": peer,
        "exports": [
            {"path": e.path, "path_raw_hex": e.path_raw.hex(), "groups": list(e.groups)}
            for e in exports
        ],
    }

    def render() -> None:
        if not exports:
            print(f"{peer}: EXPORT returned an empty list (no media inserted?)")
            return
        print(f"{peer} exports:")
        for export in exports:
            print(f"  {export}")
        print()
        print("Compare against research/06 §3 (SD=/B/, USB=/C/, rekordbox=/),")
        print("which is confirmed only against XDJ-class hardware. The raw bytes")
        print("above are the primary evidence for experiment E3.")

    _emit(args, payload, render)
    client.close()
    return 0


def _mount_slot(client: NfsClient, args: argparse.Namespace) -> tuple[bytes, str, MediaSlot]:
    slot = slot_from_name(args.slot)
    export = args.export or export_path_for(slot)
    return client.mount(export), export, slot


def cmd_mount(ctx: Context) -> int:
    args = ctx.args
    peer = ctx.resolve(args.device)
    client = ctx.nfs(peer)
    try:
        fhandle, export, slot = _mount_slot(client, args)
    except mountd.MountError as exc:
        _warn(f"MNT failed: {exc}")
        if exc.status == nfs2.Stat.NFSERR_ACCES:
            _warn("")
            _warn("This is the status libcdj reported (experiment E2). Re-run with:")
            _warn("  --auth null                 (H1: is AUTH_UNIX actually required?)")
            _warn("  --stamp 0xdeadbeef          (H1: does the stamp value matter?)")
            _warn("  --source-port 1023          (H3: reserved port required? needs sudo)")
            _warn("and against a slot that definitely has media in it (H2).")
        client.close()
        return 1
    except (ProlinkError, RpcTimeout) as exc:
        _warn(f"mount failed: {exc}")
        client.close()
        return 1

    payload = {
        "peer": peer, "slot": slot.name, "export": export,
        "fhandle_hex": fhandle.hex(), "mountd_port": client.mountd_port,
        "nfsd_port": client.nfsd_port,
    }

    def render() -> None:
        print(f"{peer} {slot.name} export={export!r}")
        print(f"  mountd UDP {client.mountd_port}, nfsd UDP {client.nfsd_port}")
        print(f"  root filehandle: {fhandle.hex()}")

    _emit(args, payload, render)
    client.close()
    return 0


# -- M4: stat / ls / fetch / pull-db ---------------------------------------


def cmd_stat(ctx: Context) -> int:
    args = ctx.args
    peer = ctx.resolve(args.device)
    client = ctx.nfs(peer)
    try:
        root, export, slot = _mount_slot(client, args)
        fhandle, attrs = client.lookup_path(root, args.path)
    except (ProlinkError, RpcTimeout) as exc:
        _warn(f"stat failed: {exc}")
        client.close()
        return 1

    payload = {
        "peer": peer, "slot": slot.name, "path": args.path,
        "fhandle_hex": fhandle.hex(), "attrs": attrs.__dict__,
    }

    def render() -> None:
        print(f"{peer} {slot.name}:{args.path}")
        print(f"  {attrs}")
        print(f"  filehandle: {fhandle.hex()}")

    _emit(args, payload, render)
    client.close()
    return 0


def cmd_ls(ctx: Context) -> int:
    """Experiment E5/E6: does READDIR work, and is it PIONEER or .PIONEER?"""
    args = ctx.args
    peer = ctx.resolve(args.device)
    client = ctx.nfs(peer)
    try:
        root, _export, slot = _mount_slot(client, args)
        fhandle, _attrs = (
            client.lookup_path(root, args.path) if args.path else (root, None)
        )
        entries, eof = client.readdir(fhandle)
    except (ProlinkError, RpcTimeout) as exc:
        _warn(f"READDIR failed: {exc}")
        _warn(
            "libcdj saw 'procedure unavailable' here. If that reproduces, record "
            "it for experiment E5 and fall back to probing known names with LOOKUP."
        )
        client.close()
        return 1

    payload = {
        "peer": peer, "slot": slot.name, "path": args.path, "eof": eof,
        "entries": [
            {"name": e.name, "name_raw_hex": e.name_raw.hex(), "fileid": e.fileid}
            for e in entries
        ],
    }

    def render() -> None:
        print(f"{peer} {slot.name}:{args.path or '/'}  ({len(entries)} entries, eof={eof})")
        for entry in entries:
            print(f"  {entry.fileid:>10}  {entry.name!r:<32} {entry.name_raw.hex()}")

    _emit(args, payload, render)
    client.close()
    return 0


def cmd_nfsprobe(ctx: Context) -> int:
    """Experiment E5: call every NFSv2 procedure and tabulate what happens.

    The serve side needs to know which procedures real CDJ firmware will call
    against us, and neither reference client exercises more than three of
    them. So establish the answer by measurement rather than by assumption.
    """
    args = ctx.args
    peer = ctx.resolve(args.device)
    client = ctx.nfs(peer)
    results: dict[str, str] = {}

    try:
        root, _export, slot = _mount_slot(client, args)
    except (ProlinkError, RpcTimeout) as exc:
        _warn(f"mount failed, cannot probe: {exc}")
        client.close()
        return 1

    probes = [
        ("NULL", lambda: client.rpc.call(
            nfs2.PROGRAM, nfs2.VERSION, nfs2.Proc.NULL, client.nfsd_port, b"", label="NULL")),
        ("GETATTR", lambda: client.getattr(root)),
        ("STATFS", lambda: client.statfs(root)),
        ("READDIR", lambda: client.readdir(root)),
        ("LOOKUP(PIONEER)", lambda: client.lookup(root, "PIONEER")),
        ("LOOKUP(.PIONEER)", lambda: client.lookup(root, ".PIONEER")),
    ]
    for label, probe in probes:
        try:
            value = probe()
            results[label] = f"OK  {value if not isinstance(value, tuple) else ''}".strip()
        except Exception as exc:
            results[label] = f"{type(exc).__name__}: {exc}"
        _warn(f"  {label:<20} {results[label]}")

    def render() -> None:
        print(f"{peer} {slot.name} NFSv2 procedure probe:")
        for label, outcome in results.items():
            print(f"  {label:<20} {outcome}")
        print()
        print("LOOKUP(PIONEER) vs LOOKUP(.PIONEER) settles experiment E6.")

    _emit(args, {"peer": peer, "slot": slot.name, "probes": results}, render)
    client.close()
    return 0


def _fetch(ctx: Context, client: NfsClient, root: bytes, remote_path: str, destination: Path):
    args = ctx.args
    fhandle, attrs = client.lookup_path(root, remote_path)
    if attrs.is_directory:
        raise SystemExit(f"{remote_path} is a directory, not a file")

    _warn(f"fetching {remote_path} ({attrs.size} bytes) -> {destination}")
    stats = DownloadStats()
    last = [0.0]

    def on_progress(done: int, total: int) -> None:
        now = time.monotonic()
        if now - last[0] < 0.2 and done < total:
            return
        last[0] = now
        percent = (100.0 * done / total) if total else 100.0
        print(f"\r  {done}/{total} bytes ({percent:5.1f}%)", end="", file=sys.stderr)

    client.download_to_file(
        fhandle, attrs.size, destination,
        chunk=args.chunk, window=args.window,
        on_progress=on_progress, stats=stats,
    )
    print(file=sys.stderr)
    return attrs, stats


def cmd_fetch(ctx: Context) -> int:
    args = ctx.args
    peer = ctx.resolve(args.device)
    client = ctx.nfs(peer)
    try:
        root, _export, slot = _mount_slot(client, args)
        destination = Path(args.output) if args.output else Path(Path(args.path).name)
        attrs, stats = _fetch(ctx, client, root, args.path, destination)
    except (ProlinkError, RpcTimeout, OSError) as exc:
        _warn(f"fetch failed: {exc}")
        client.close()
        return 1

    digest = _sha256(destination)
    payload = {
        "peer": peer, "slot": slot.name, "path": args.path,
        "output": str(destination), "size": attrs.size, "sha256": digest,
        "requests": stats.requests, "short_reads": stats.short_reads,
        "elapsed_s": round(stats.elapsed, 3),
        "throughput_kib_s": round(stats.throughput_kbs, 1),
        "chunk": args.chunk, "window": args.window,
    }

    def render() -> None:
        print(f"wrote {destination} ({attrs.size} bytes)")
        print(f"  sha256 {digest}")
        print(f"  {stats}")
        print()
        print("Verify against the physically-mounted stick -- this is the anchor")
        print("test for milestone M4:")
        print(f"  shasum -a 256 /Volumes/<STICK>/{args.path}")

    _emit(args, payload, render)
    client.close()
    return 0


def cmd_pull_db(ctx: Context) -> int:
    args = ctx.args
    peer = ctx.resolve(args.device)
    client = ctx.nfs(peer)
    try:
        root, _export, slot = _mount_slot(client, args)
        destination = Path(args.output) if args.output else Path("export.pdb")
        attrs, stats = _fetch(ctx, client, root, PDB_PATH, destination)
    except (ProlinkError, RpcTimeout, OSError) as exc:
        _warn(f"could not pull {PDB_PATH}: {exc}")
        _warn("If this is NFSERR_NOENT, try '.PIONEER' -- see experiment E6, and")
        _warn("use 'prolinks nfsprobe' to settle which spelling this media uses.")
        client.close()
        return 1

    digest = _sha256(destination)
    payload = {
        "peer": peer, "slot": slot.name, "path": PDB_PATH,
        "output": str(destination), "size": attrs.size, "sha256": digest,
        "elapsed_s": round(stats.elapsed, 3),
    }

    def render() -> None:
        print(f"pulled {PDB_PATH} -> {destination} ({attrs.size} bytes)")
        print(f"  sha256 {digest}")
        print(f"  {stats}")

    _emit(args, payload, render)
    client.close()
    return 0


def _sha256(path: Path) -> str:
    import hashlib

    digest = hashlib.sha256()
    with Path(path).open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()



# -- M9: announce ----------------------------------------------------------


def cmd_announce(ctx: Context) -> int:
    """The only command that transmits on a DJ-Link port."""
    args = ctx.args
    if args.assert_passive and not args.dry_run:
        raise SystemExit(
            "--assert-passive and 'announce' are contradictory: announcing is "
            "transmitting. Use --dry-run to build the packets without sending."
        )

    interface = ctx.interface
    discovery = PassiveDiscovery(
        ctx.loop, recorder=ctx.recorder, guard=ctx.guard,
        via_interface=interface.name,
    )
    discovery.start()

    virtual = VirtualCdj(
        ctx.loop,
        discovery,
        interface,
        device_number=args.number,
        name=args.name,
        claim=args.claim,
        dry_run=args.dry_run,
        trailing=args.trailing,
        emit_status=args.status,
        has_usb=args.has_usb,
        recorder=ctx.recorder,
        on_state=lambda state, message: _warn(f"  [{state.value}] {message}"),
    )
    # An active announcer must defend its number or it will simply lose it to
    # the next player that boots (research/02 §1.5).
    discovery.on_claim = virtual.defend

    _warn(f"interface {interface}")
    _warn(f"broadcasting to {interface.broadcast}:{djl.DISCOVERY_PORT}")
    _warn(f"announcing as {args.name!r} device {args.number}"
          f"{' with full claim handshake' if args.claim else ' (keep-alive only)'}")
    if args.dry_run:
        _warn("DRY RUN -- packets are built and logged but never sent")

    keepalive = virtual.build_keepalive()
    _warn("")
    _warn("keep-alive we will emit every 1.5s:")
    _warn(f"  {keepalive.encode().hex()}")
    _warn("  (M9 acceptance: diff this against a real CDJ-2000nexus keep-alive")
    _warn("   from the journal and justify every differing byte)")
    _warn("")

    virtual.start()
    try:
        if args.duration:
            ctx.loop.run_for(args.duration)
        else:
            ctx.loop.run_until(predicate=lambda: False)
    except KeyboardInterrupt:
        _warn("interrupted")
    finally:
        virtual.stop()

    payload = {
        "state": virtual.state.value,
        "device_number": virtual.device_number,
        "name": args.name,
        "claimed": args.claim,
        "dry_run": args.dry_run,
        "keepalive_hex": keepalive.encode().hex(),
        "conflicts": virtual.conflicts,
        "dry_run_packets": [packet.hex() for packet in virtual.sent],
        "peers": [d.label() for d in discovery.table.all()],
    }

    def render() -> None:
        print(f"final state: {virtual.state.value}, device number {virtual.device_number}")
        if virtual.conflicts:
            print(f"conflicts encountered on: {virtual.conflicts}")
        if virtual.sent:
            print(f"dry-run packets built: {len(virtual.sent)}")
            for packet in virtual.sent:
                print(f"  {packet.hex()}")
        print(f"peers seen: {[d.label() for d in discovery.table.all()] or 'none'}")

    _emit(args, payload, render)
    discovery.close()
    return 0 if virtual.state is not AnnouncerState.FAILED else 1


# -- capture analysis ------------------------------------------------------


def cmd_pcap(ctx: Context) -> int:
    """Dissect a capture offline. No network access at all.

    Lets the codecs be exercised against real Pioneer traffic before any
    hardware is plugged in -- which is how FINDINGS C1-C4 and C6-C9 were found.
    """
    args = ctx.args
    path = Path(args.file)
    counts: Counter = Counter()
    decoded_kinds: Counter = Counter()
    names: Counter = Counter()
    mismatches = 0
    checked = 0

    for packet in read_capture(path):
        counts[(packet.protocol, packet.dst_port)] += 1
        if packet.protocol != "udp" or packet.dst_port != djl.DISCOVERY_PORT:
            continue
        if not djl.is_djl_packet(packet.payload):
            continue
        try:
            message = djl.decode(packet.payload)
        except ProlinkError as exc:
            decoded_kinds[f"UNDECODABLE({exc})"] += 1
            continue
        decoded_kinds[type(message).__name__] += 1
        names[message.name_raw] += 1
        if not isinstance(message, djl.UnknownPacket):
            checked += 1
            if message.encode() != packet.payload:
                mismatches += 1
                if args.verbose:
                    print(f"  mismatch #{packet.index}: "
                          f"real {packet.payload.hex()} != ours {message.encode().hex()}")
        if args.decode:
            print(f"{packet}  {message}")

    payload = {
        "file": str(path),
        "ports": {f"{proto}/{port}": n for (proto, port), n in counts.most_common()},
        "djl_packets": dict(decoded_kinds),
        "round_trip_checked": checked,
        "round_trip_mismatches": mismatches,
        "names": {raw.hex(): n for raw, n in names.most_common()},
    }

    def render() -> None:
        print(f"{path}: {sum(counts.values())} transport packets")
        print()
        print("  by destination port:")
        for (proto, port), n in counts.most_common(12):
            note = {
                50000: "  DJ-Link discovery/keep-alive",
                50001: "  DJ-Link beat",
                50002: "  DJ-Link status",
                111: "  SUN RPC portmapper",
                12523: "  dbserver port discovery",
                1051: "  dbserver",
            }.get(port, "")
            print(f"    {proto}/{port:<6} {n:>6}{note}")
        if decoded_kinds:
            print()
            print("  DJ-Link packets decoded:")
            for kind, n in decoded_kinds.most_common():
                print(f"    {kind:<20} {n}")
            print()
            print(f"  round-trip: {checked - mismatches}/{checked} byte-exact")
            print()
            print("  literal 20-byte name fields:")
            for raw, n in names.most_common():
                label = raw.split(b"\x00", 1)[0].decode("ascii", errors="replace")
                print(f"    {label!r:<24} {raw.hex()}  x{n}")

    _emit(args, payload, render)
    return 0 if mismatches == 0 else 1



# -- M5: library (pdb) -----------------------------------------------------


def _load_library(ctx: Context) -> tuple[Library, str]:
    """Load a library from a local file, a mounted volume, or a CDJ over NFS."""
    args = ctx.args
    if args.file:
        source = args.file
        _warn(f"reading {source}")
        return Library.from_file(source), source

    if args.volume:
        for candidate in ("PIONEER", ".PIONEER"):
            # HFS-formatted media hides the directory behind a leading dot;
            # there is no way to tell which without looking (research/05 §1).
            path = Path(args.volume) / candidate / "rekordbox" / "export.pdb"
            if path.exists():
                _warn(f"reading {path}")
                return Library.from_file(path), str(path)
        raise SystemExit(
            f"no PIONEER/rekordbox/export.pdb or .PIONEER/... under {args.volume}"
        )

    if not args.device:
        raise SystemExit("give one of: --file, --volume, or a device")

    peer = ctx.resolve(args.device)
    client = ctx.nfs(peer)
    try:
        root, _export, slot = _mount_slot(client, args)
        for candidate in ("PIONEER", ".PIONEER"):
            remote = f"{candidate}/rekordbox/export.pdb"
            try:
                fhandle, attrs = client.lookup_path(root, remote)
            except nfs2.NfsError as exc:
                if exc.is_missing:
                    continue
                raise
            _warn(f"fetching {remote} ({attrs.size} bytes) from {peer}")
            data = client.download(fhandle, attrs.size, chunk=args.chunk, window=args.window)
            return Library.from_bytes(data), f"{peer}:{slot.name}/{remote}"
        raise SystemExit(f"{peer} has no rekordbox database in the {slot.name} slot")
    finally:
        client.close()


def cmd_tracks(ctx: Context) -> int:
    args = ctx.args
    library, source = _load_library(ctx)
    tracks = library.search(args.search) if args.search else library.track_list()

    payload = {
        "source": source,
        "summary": library.summary(),
        "tracks": [
            {
                "id": t.id, "title": t.title, "artist": t.artist, "album": t.album,
                "genre": t.genre, "key": t.key, "bpm_100": t.bpm_100,
                "duration": t.duration, "rating": t.rating, "year": t.year,
                "path": t.path, "analyze_path": t.analyze_path,
                "artwork_path": t.artwork_path, "file_size": t.file_size,
            }
            for t in tracks
        ],
    }

    def render() -> None:
        print(f"{source}")
        print("  " + "  ".join(f"{k}={v}" for k, v in library.summary().items()))
        print()
        print(f"{'id':>8}  {'time':>6}  {'bpm':>6}  {'key':<4} {'artist':<28}  title")
        for track in tracks[: args.limit]:
            print(track)
        if len(tracks) > args.limit:
            print(f"... {len(tracks) - args.limit} more (raise --limit)")

    _emit(args, payload, render)
    return 0


def cmd_playlists(ctx: Context) -> int:
    args = ctx.args
    library, source = _load_library(ctx)

    payload = {
        "source": source,
        "playlists": [
            {
                "id": p.id, "name": p.name, "parent_id": p.parent_id,
                "is_folder": p.is_folder, "track_count": p.track_count,
                "track_ids": p.track_ids,
            }
            for p in library.playlists.values()
        ],
    }

    def render() -> None:
        print(f"{source}")
        for line in library.format_playlist_tree():
            print(line)
        if args.show:
            print()
            print(f"tracks in playlist #{args.show}:")
            for track in library.playlist_tracks(args.show):
                print(f"  {track}")

    _emit(args, payload, render)
    return 0


def cmd_pdb_dump(ctx: Context) -> int:
    """Structural dump of a pdb. Offline; the first thing to run on a new stick."""
    args = ctx.args
    library, source = _load_library(ctx)
    summary = library.pdb.table_summary()

    payload = {"source": source, "tables": summary, "library": library.summary()}

    def render() -> None:
        print(f"{source}")
        print(f"  page size {library.pdb.page_size}, {library.pdb.page_entries} table pointers")
        print()
        print("  rows per table:")
        for name, count in summary.items():
            print(f"    {name:<18} {count}")
        print()
        print("  resolved library:")
        for name, count in library.summary().items():
            print(f"    {name:<18} {count}")

    _emit(args, payload, render)
    return 0



# -- dbserver: browsing another player --------------------------------------


def cmd_db_browse(ctx: Context) -> int:
    """Browse a player's library over dbserver, the way a CDJ's LINK button does.

    Needs a device number in 1-4 that belongs to a device actually on the
    network and is not the player being queried (``research/04`` §2.3) -- which
    is why ``--as`` exists and why the NFS path, which needs no number at all,
    is the safer default.
    """
    args = ctx.args
    peer = ctx.resolve(args.device)
    slot = slot_from_name(args.slot)

    try:
        port = args.port or discover_port(peer, args.timeout)
    except DbServerUnavailable as exc:
        _warn(f"{exc}")
        _warn("The player may not be running a dbserver, or may not be reachable.")
        return 1
    _warn(f"{peer} dbserver is on port {port}")

    try:
        client = DbClient(peer, args.device_number, port=port,
                          timeout=args.timeout, recorder=ctx.recorder)
        client.connect()
    except (ProlinkError, OSError) as exc:
        _warn(f"could not connect: {exc}")
        _warn(
            f"If this was rejected, --as {args.device_number} may be invalid: it must be "
            "1-4, must belong to a device present on the network, and must not be "
            "the player you are querying."
        )
        return 1

    try:
        if args.what == "root":
            items = client.root_menu(slot)
        elif args.what == "tracks":
            items = client.track_list(slot)
        elif args.what == "playlists":
            items = client.playlists(slot, args.id, folder=not args.tracks)
        elif args.what == "search":
            items = client.menu(dbproto.MessageType.MENU_SEARCH, slot, 0, args.term or "")
        elif args.what == "metadata":
            metadata = client.track_metadata(slot, args.id)
            _emit(args, {"peer": peer, "metadata": metadata},
                  lambda: [print(f"  {k:<12} {v}") for k, v in sorted(metadata.items())])
            return 0
        else:
            items = client.track_list(slot)
    except (ProlinkError, OSError) as exc:
        _warn(f"query failed: {exc}")
        return 1
    finally:
        if args.what != "metadata":
            pass

    payload = {
        "peer": peer, "port": port, "slot": slot.name,
        "server_device_number": client.server_device_number,
        "items": [
            {"id": i.id, "parent_id": i.parent_id, "label1": i.label1,
             "label2": i.label2, "item_type": i.type_name,
             "artwork_id": i.artwork_id, "position": i.playlist_position}
            for i in items
        ],
    }

    def render() -> None:
        print(f"{peer}:{port} {slot.name}  (peer is device {client.server_device_number})")
        print(f"{len(items)} items")
        for item in items:
            print(f"  {item}")

    _emit(args, payload, render)
    client.close()
    return 0


# -- serve: expose a local rekordbox volume to real CDJs ---------------------


def cmd_serve(ctx: Context) -> int:
    """Serve a rekordbox volume to real players: NFS + dbserver + announcing.

    Three surfaces have to be up at once for a CDJ to see and browse us:

    * **announcing** on UDP 50000, or nothing knows we exist;
    * **dbserver** on TCP, which is what the LINK button actually drives;
    * **NFS**, which is how the files themselves are read.

    The NFS portmapper must bind UDP/111, which needs root. Everything else
    works unprivileged, so ``--no-nfs`` gives a useful dbserver-only mode for
    testing without sudo.
    """
    args = ctx.args

    volume = Path(args.volume) if args.volume else None
    if volume is None:
        raise SystemExit("--volume is required: point it at a mounted rekordbox stick")
    if not volume.is_dir():
        raise SystemExit(f"{volume} is not a directory")

    pioneer = next(
        (volume / name for name in ("PIONEER", ".PIONEER") if (volume / name).is_dir()),
        None,
    )
    if pioneer is None:
        raise SystemExit(f"{volume} has no PIONEER or .PIONEER directory")
    pdb_path = pioneer / "rekordbox" / "export.pdb"
    if not pdb_path.exists():
        raise SystemExit(f"no rekordbox database at {pdb_path}")

    _warn(f"loading {pdb_path}")
    library = Library.from_file(pdb_path)
    _warn("  " + "  ".join(f"{k}={v}" for k, v in library.summary().items()))

    interface = ctx.interface
    _warn(f"interface {interface}")

    # dbserver first: it is the surface a CDJ's LINK button drives, and it
    # needs no privileges.
    db_server = DbServer(
        library,
        device_number=args.number,
        slot=slot_from_name(args.slot),
        bind_ip="0.0.0.0",
        port=args.db_port,
        query_port=dbproto.QUERY_PORT,
        media_root=volume,
        recorder=ctx.recorder,
    ).start()
    _warn(f"dbserver on TCP {db_server.port} (port query on {db_server.query_port})")

    nfs_server = None
    if args.nfs:
        _warn(f"indexing {volume} for NFS (contents are read lazily) ...")
        vfs = Vfs.from_directory(volume)
        try:
            nfs_server = NfsServer(
                ctx.loop, vfs,
                exports={export_path_for(slot_from_name(args.slot)): "/"},
                bind_ip="0.0.0.0", portmap_port=args.portmap_port,
                recorder=ctx.recorder,
            )
            nfs_server.start()
            _warn(
                f"NFS: portmap {nfs_server.portmap_port}, mountd {nfs_server.mountd_port}, "
                f"nfsd {nfs_server.nfsd_port}"
            )
            if nfs_server.portmap_port != portmap.PORT:
                _warn(
                    f"  WARNING: portmap is on {nfs_server.portmap_port}, not {portmap.PORT}. "
                    "Real players only look on 111 -- re-run with sudo."
                )
        except PermissionError:
            _warn(
                f"could not bind UDP {portmap.PORT} (needs root). Continuing without NFS; "
                "re-run with sudo to serve files."
            )

    discovery = PassiveDiscovery(
        ctx.loop, recorder=ctx.recorder, guard=ctx.guard,
        via_interface=interface.name,
    )
    discovery.start()

    virtual = None
    if args.announce:
        virtual = VirtualCdj(
            ctx.loop, discovery, interface,
            device_number=args.number, name=args.name, claim=args.claim,
            # Without status packets a player sees us as a deck with empty
            # slots, however loudly we announce (FINDINGS F20/F21).
            emit_status=True, has_usb=True, recorder=ctx.recorder,
            media_name=volume.name,
            track_count=len(library.tracks),
            playlist_count=sum(1 for p in library.playlists.values() if not p.is_folder),
            on_state=lambda state, message: _warn(f"  [{state.value}] {message}"),
        )
        discovery.on_claim = virtual.defend
        virtual.start()
        _warn(f"announcing as {args.name!r} device {args.number}")
    else:
        _warn("not announcing (--no-announce); players will not discover us")

    _warn("")
    _warn("serving -- Ctrl-C to stop")
    try:
        if args.duration:
            ctx.loop.run_for(args.duration)
        else:
            ctx.loop.run_until(predicate=lambda: False)
    except KeyboardInterrupt:
        _warn("interrupted")
    finally:
        if virtual is not None:
            virtual.stop()
        if nfs_server is not None:
            nfs_server.close()
        db_server.stop()
        discovery.close()

    def render() -> None:
        print("dbserver requests served:")
        for name, count in sorted(db_server.stats.items()):
            print(f"  {name:<16} {count}")
        print(f"peers seen: {[d.label() for d in discovery.table.all()] or 'none'}")

    _emit(args, {"dbserver_requests": db_server.stats,
                 "peers": [d.label() for d in discovery.table.all()]}, render)
    return 0


# -- argument parsing ------------------------------------------------------



def _add_global_options(parser: argparse.ArgumentParser, suppress: bool = False) -> None:
    """Options accepted both before and after the subcommand.

    With *suppress*, an omitted flag leaves the attribute unset rather than
    writing a default -- which is what stops the subcommand copy from clobbering
    a value the user gave before the subcommand.
    """
    default = {"default": argparse.SUPPRESS} if suppress else {}
    parser.add_argument(
        "--iface", help="interface facing the CDJs (default: auto)", **default
    )
    parser.add_argument(
        "--capture-dir", help="where to write the capture journal", **default
    )
    parser.add_argument(
        "--no-record", dest="record", action="store_false",
        help="do not write a capture journal",
        **(default or {"default": True}),
    )
    parser.add_argument(
        "--assert-passive", action="store_true",
        help="fail if anything transmits on a DJ-Link port (experiment E1)",
        **(default or {"default": False}),
    )
    parser.add_argument(
        "--notes", help="operator note recorded in the journal", **default
    )
    parser.add_argument(
        "--json", action="store_true", help="machine-readable output",
        **(default or {"default": False}),
    )
    parser.add_argument(
        "-v", "--verbose", action="count", **(default or {"default": 0})
    )
    parser.add_argument(
        "--timeout", type=float, help="RPC reply timeout in seconds",
        **(default or {"default": 2.0}),
    )
    parser.add_argument(
        "--retries", type=int, help="RPC retry count", **(default or {"default": 5})
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="prolinks",
        description="ProLink proof-of-concept: passive discovery and NFS file access.",
        epilog="Milestones and experiments: research/10-mixxx-prolink-implementation-plan.md",
    )
    _add_global_options(parser)

    # The same flags again, on every subcommand, so `prolinks rpcinfo <ip>
    # --notes "..."` works as well as `prolinks --notes "..." rpcinfo <ip>`.
    # argparse normally requires a parent-parser option to precede the
    # subcommand, which is a trap when you are typing at 1 a.m. with a deck
    # booting in front of you. SUPPRESS is what makes it safe: without it the
    # subparser's default would overwrite a value already given to the parent.
    trailing = argparse.ArgumentParser(add_help=False)
    _add_global_options(trailing, suppress=True)

    subparsers = parser.add_subparsers(dest="command", required=True)
    _original_add_parser = subparsers.add_parser

    def add_parser(name, **kwargs):
        kwargs.setdefault("parents", []).append(trailing)
        return _original_add_parser(name, **kwargs)

    subparsers.add_parser = add_parser

    def add_rpc_options(sub) -> None:
        """Options shared by every command that speaks RPC."""
        sub.add_argument("device", help="player number, or an IP (an IP skips discovery)")
        sub.add_argument(
            "--auth", choices=["unix", "null"], default="unix",
            help="RPC credential flavour (experiment E2)",
        )
        sub.add_argument(
            "--stamp", type=lambda v: int(v, 0), default=None,
            help="AUTH_UNIX stamp, e.g. 0xdeadbeef (experiment E2)",
        )
        sub.add_argument(
            "--source-port", type=int, default=0,
            help="bind a specific source port; <1024 needs root (experiment E2 H3)",
        )

    def add_slot_options(sub) -> None:
        sub.add_argument(
            "--slot", default="usb", help="usb | sd | rb (default: usb)"
        )
        sub.add_argument(
            "--export", help="override the export path instead of deriving it from --slot"
        )

    def add_transfer_options(sub) -> None:
        sub.add_argument("--chunk", type=int, default=DEFAULT_CHUNK, help="bytes per READ")
        sub.add_argument("--window", type=int, default=DEFAULT_WINDOW, help="READs in flight")
        sub.add_argument("-o", "--output", help="output path")

    sniff = subparsers.add_parser("sniff", help="M0: hex-dump DJ-Link traffic")
    sniff.add_argument("--ports", default="50000,50001,50002")
    sniff.add_argument("--duration", type=float, default=15.0)
    sniff.add_argument("--decode", action="store_true", help="also decode port 50000")
    sniff.add_argument("--hex", action="store_true", help="hex dump every datagram")
    sniff.set_defaults(func=cmd_sniff)

    devices = subparsers.add_parser("devices", help="M1: list peers heard on the network")
    devices.add_argument("--watch", action="store_true", help="keep listening until Ctrl-C")
    devices.add_argument(
        "--timeout-s", type=float, default=DEFAULT_DISCOVERY_WAIT_S,
        help="how long to listen when not watching",
    )
    devices.set_defaults(func=cmd_devices)

    interfaces = subparsers.add_parser("interfaces", help="list local network interfaces")
    interfaces.set_defaults(func=cmd_interfaces)

    rpcinfo = subparsers.add_parser("rpcinfo", help="M2: probe the RPC stack (GO/NO-GO gate)")
    add_rpc_options(rpcinfo)
    rpcinfo.set_defaults(func=cmd_rpcinfo)

    exports = subparsers.add_parser("exports", help="M3: list MOUNT exports (experiment E3)")
    add_rpc_options(exports)
    exports.set_defaults(func=cmd_exports)

    mount = subparsers.add_parser("mount", help="M3: MNT a slot, print the root filehandle")
    add_rpc_options(mount)
    add_slot_options(mount)
    mount.set_defaults(func=cmd_mount)

    stat = subparsers.add_parser("stat", help="M4: LOOKUP a path and show its attributes")
    add_rpc_options(stat)
    add_slot_options(stat)
    stat.add_argument("--path", required=True)
    stat.set_defaults(func=cmd_stat)

    listing = subparsers.add_parser("ls", help="M4: READDIR a directory (experiments E5/E6)")
    add_rpc_options(listing)
    add_slot_options(listing)
    listing.add_argument("--path", default="")
    listing.set_defaults(func=cmd_ls)

    probe = subparsers.add_parser("nfsprobe", help="E5: call every NFSv2 procedure")
    add_rpc_options(probe)
    add_slot_options(probe)
    probe.set_defaults(func=cmd_nfsprobe)

    fetch = subparsers.add_parser("fetch", help="M4: download a file over NFS")
    add_rpc_options(fetch)
    add_slot_options(fetch)
    add_transfer_options(fetch)
    fetch.add_argument("--path", required=True, help="path relative to the export root")
    fetch.set_defaults(func=cmd_fetch)

    pull = subparsers.add_parser("pull-db", help="M4: download export.pdb")
    add_rpc_options(pull)
    add_slot_options(pull)
    add_transfer_options(pull)
    pull.set_defaults(func=cmd_pull_db)

    announce = subparsers.add_parser(
        "announce", help="M9: BROADCAST as a virtual CDJ (the only transmitting command)"
    )
    announce.add_argument(
        "--number", type=int, default=SAFE_OBSERVER_NUMBER,
        help=f"device number (default {SAFE_OBSERVER_NUMBER}: outside 1-6, cannot collide, "
             "but also cannot issue dbserver queries)",
    )
    announce.add_argument("--name", default="CDJ-2000nexus", help="20-byte device name")
    announce.add_argument(
        "--claim", action="store_true",
        help=f"run the full device-number claim handshake to take a real player slot "
             f"{PLAYER_NUMBERS} -- can disturb a live rig",
    )
    announce.add_argument(
        "--dry-run", action="store_true", help="build and print the packets without sending"
    )
    announce.add_argument(
        "--trailing", type=lambda v: int(v, 0), default=0x00,
        help="keep-alive byte 0x35: 0x00 nexus (observed), 0x64 CDJ-3000 coexistence",
    )
    announce.add_argument(
        "--status", action="store_true",
        help="also emit CDJ status packets on 50002 -- required for a player to "
             "believe we have media (FINDINGS F20/F21)",
    )
    announce.add_argument(
        "--has-usb", action="store_true",
        help="with --status: claim a loaded USB slot",
    )
    announce.add_argument("--duration", type=float, default=0.0, help="0 = until Ctrl-C")
    announce.set_defaults(func=cmd_announce)

    pcap = subparsers.add_parser("pcap", help="dissect a pcap/pcapng offline (no network)")
    pcap.add_argument("file")
    pcap.add_argument("--decode", action="store_true", help="print every DJ-Link packet")
    pcap.set_defaults(func=cmd_pcap)

    def add_library_source(sub) -> None:
        """A library can come from a local file, a mounted volume, or a CDJ."""
        sub.add_argument("device", nargs="?", help="player number or IP to fetch from")
        sub.add_argument("--file", help="a local export.pdb")
        sub.add_argument("--volume", help="a mounted rekordbox volume, e.g. /Volumes/USB")
        sub.add_argument("--slot", default="usb", help="usb | sd | rb (when fetching)")
        sub.add_argument("--export", help="override the export path")
        sub.add_argument("--chunk", type=int, default=DEFAULT_CHUNK)
        sub.add_argument("--window", type=int, default=DEFAULT_WINDOW)
        sub.add_argument("--auth", choices=["unix", "null"], default="unix")
        sub.add_argument("--stamp", type=lambda v: int(v, 0), default=None)
        sub.add_argument("--source-port", type=int, default=0)

    tracks = subparsers.add_parser("tracks", help="M5: list tracks from a rekordbox database")
    add_library_source(tracks)
    tracks.add_argument("--search", help="filter on title/artist/album")
    tracks.add_argument("--limit", type=int, default=100)
    tracks.set_defaults(func=cmd_tracks)

    playlists = subparsers.add_parser("playlists", help="M5: show the playlist tree")
    add_library_source(playlists)
    playlists.add_argument("--show", type=int, help="also list the tracks in this playlist id")
    playlists.set_defaults(func=cmd_playlists)

    pdb_dump = subparsers.add_parser("pdb-dump", help="M5: structural dump of a pdb")
    add_library_source(pdb_dump)
    pdb_dump.set_defaults(func=cmd_pdb_dump)

    browse = subparsers.add_parser(
        "db-browse", help="browse a player's library over dbserver (like the LINK button)"
    )
    browse.add_argument("device", help="player number or IP")
    browse.add_argument(
        "--as", dest="device_number", type=int, default=1,
        help="the device number to identify as; must be 1-4 (research/04 §2.3)",
    )
    browse.add_argument("--slot", default="usb", help="usb | sd | rb")
    browse.add_argument("--port", type=int, default=0, help="skip the 12523 port query")
    browse.add_argument(
        "--what", default="tracks",
        choices=["root", "tracks", "playlists", "metadata", "search"],
    )
    browse.add_argument("--id", type=int, default=0, help="playlist or track id")
    browse.add_argument("--tracks", action="store_true",
                        help="with --what playlists: list a playlist's tracks")
    browse.add_argument("--term", help="with --what search")
    browse.set_defaults(func=cmd_db_browse)

    serve = subparsers.add_parser(
        "serve", help="serve a mounted rekordbox volume to real CDJs"
    )
    serve.add_argument("--volume", required=True, help="e.g. /Volumes/MYUSB")
    serve.add_argument("--slot", default="usb", help="which slot to present as")
    serve.add_argument("--number", type=int, default=5, help="our device number")
    serve.add_argument("--name", default="CDJ-2000nexus", help="20-byte device name")
    serve.add_argument("--claim", action="store_true", help="claim a real player slot")
    serve.add_argument("--db-port", type=int, default=0, help="0 = pick a free port")
    serve.add_argument(
        "--portmap-port", type=int, default=portmap.PORT,
        help="only change for testing; real players look on 111 only",
    )
    serve.add_argument("--no-nfs", dest="nfs", action="store_false",
                       help="dbserver only; skips the port-111 bind that needs root")
    serve.add_argument("--no-announce", dest="announce", action="store_false")
    serve.add_argument("--duration", type=float, default=0.0, help="0 = until Ctrl-C")
    serve.set_defaults(func=cmd_serve)

    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    logging.basicConfig(
        level=[logging.WARNING, logging.INFO, logging.DEBUG][min(args.verbose, 2)],
        format="%(levelname)s %(name)s: %(message)s",
    )

    ctx = Context(args)
    try:
        return args.func(ctx)
    except KeyboardInterrupt:
        _warn("interrupted")
        return 130
    except SystemExit:
        raise
    except ProlinkError as exc:
        _warn(f"error: {exc}")
        return 1
    except RuntimeError as exc:
        # Configuration problems -- no usable interface, an ambiguous choice --
        # are the operator's to fix, and a traceback buries the instructions.
        _warn(f"error: {exc}")
        return 1
    finally:
        ctx.finish()


if __name__ == "__main__":
    sys.exit(main())
