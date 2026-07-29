"""Validate the codecs against **real Pioneer traffic**.

Round-trip tests against our own encoder cannot catch a shared misreading of
the specification; these can, and did -- see ``FINDINGS.md`` corrections C1-C4,
all of which came from running exactly these assertions.

The captures live in ``research/ref-repos/`` (git-ignored) and are read in
place, never copied here: they are recordings of Pioneer hardware, i.e.
protocol facts, but the projects that ship them are EPL/unlicensed and there
is no reason to entangle ourselves. Tests skip when the clones are absent.
"""

from __future__ import annotations

from collections import Counter
from pathlib import Path

import pytest

from prolinks_poc.capture.pcap import read_capture, read_dump_file
from prolinks_poc.proto import djl

REPO = Path(__file__).resolve().parent.parent
DYSENTERY = REPO / "research" / "ref-repos" / "dysentery" / "doc" / "assets"
LIBCDJ_DUMPS = REPO / "research" / "ref-repos" / "libcdj" / "src" / "test"

PCAPS = ["powerup.pcapng", "to-virtual.pcapng", "LinkInfo.pcapng", "LinkInfo2.pcapng"]

needs_dysentery = pytest.mark.skipif(
    not DYSENTERY.is_dir(), reason="dysentery clone absent; see research/00-references.md"
)
needs_libcdj = pytest.mark.skipif(
    not LIBCDJ_DUMPS.is_dir(), reason="libcdj clone absent"
)


def _discovery_packets():
    for name in PCAPS:
        path = DYSENTERY / name
        if not path.exists():
            continue
        for packet in read_capture(path):
            if (
                packet.protocol == "udp"
                and packet.dst_port == djl.DISCOVERY_PORT
                and djl.is_djl_packet(packet.payload)
            ):
                yield name, packet


@needs_dysentery
def test_every_captured_packet_decodes():
    decoded = Counter()
    for _name, packet in _discovery_packets():
        decoded[type(djl.decode(packet.payload)).__name__] += 1
    assert sum(decoded.values()) > 200, "expected a few hundred DJ-Link packets"
    assert decoded["KeepAlive"] > 100


@needs_dysentery
def test_every_captured_packet_round_trips_byte_exactly():
    """The strongest assertion available without hardware.

    Decoding then re-encoding real traffic and demanding identical bytes means
    the encoder is validated against Pioneer, not merely against our own
    decoder. Every field, including the ones whose meaning we do not know,
    has to be preserved.
    """
    checked = 0
    for name, packet in _discovery_packets():
        decoded = djl.decode(packet.payload)
        if isinstance(decoded, djl.UnknownPacket):
            continue  # mixer-assignment types; not modelled, so not re-encodable
        assert decoded.encode() == packet.payload, (
            f"{name} packet #{packet.index} ({type(decoded).__name__}) "
            f"re-encoded differently:\n  real {packet.payload.hex()}\n  ours {decoded.encode().hex()}"
        )
        checked += 1
    assert checked > 200


@needs_dysentery
def test_cdj_2000nexus_name_casing_is_confirmed():
    """Closes the ``research/02`` §4.1 gap.

    The exact casing was *inferred* in every published source -- no capture
    contained a literal CDJ-2000 name field. It does now: 165 keep-alives in
    the dysentery captures carry these bytes.
    """
    names = {packet.payload[0x0C:0x20] for _name, packet in _discovery_packets()}
    expected = b"CDJ-2000nexus".ljust(20, b"\x00")
    assert expected in names
    assert expected.hex() == "43444a2d323030306e6578757300000000000000"


@needs_dysentery
def test_claim_number_is_38_bytes_not_42():
    """FINDINGS C2: ``research/02`` §0.1's length column is wrong for type 04."""
    lengths = {
        len(packet.payload)
        for _name, packet in _discovery_packets()
        if packet.payload[djl.OFF_TYPE] == djl.PacketType.CLAIM_NUMBER
    }
    assert lengths == {0x26}


@needs_dysentery
def test_claim_ip_byte_30_is_a_role_not_a_constant():
    """FINDINGS C1: a mixer sends 02 there, a CDJ sends 01."""
    by_name: dict[str, set[int]] = {}
    for _name, packet in _discovery_packets():
        if packet.payload[djl.OFF_TYPE] != djl.PacketType.CLAIM_IP:
            continue
        decoded = djl.decode(packet.payload)
        by_name.setdefault(decoded.name, set()).add(decoded.role)
    assert by_name.get("CDJ-2000nexus") == {0x01}
    assert by_name.get("DJM-2000nexus") == {0x02}


@needs_dysentery
def test_nexus_keepalive_trailing_byte_is_zero():
    """FINDINGS C3: not ``01`` as documented. Matters for impersonation."""
    trailing: dict[str, Counter] = {}
    for _name, packet in _discovery_packets():
        decoded = djl.decode(packet.payload)
        if isinstance(decoded, djl.KeepAlive):
            trailing.setdefault(decoded.name, Counter())[decoded.trailing] += 1
    assert set(trailing["CDJ-2000nexus"]) == {0x00}
    assert set(trailing["DJM-2000nexus"]) == {0x00}
    # ...and our default matches what the hardware actually sends.
    assert djl.KeepAlive(
        name="CDJ-2000nexus", name_raw=b"", device_kind=djl.DeviceKind.CDJ,
        device_number=2, mac=bytes(6), ip="1.2.3.4",
    ).encode()[0x35] == 0x00


@needs_dysentery
def test_observed_device_numbers_match_the_documented_ranges():
    """research/02 §3.1: players 1-4, mixer 0x21 (33)."""
    numbers: dict[str, set[int]] = {}
    for _name, packet in _discovery_packets():
        decoded = djl.decode(packet.payload)
        if isinstance(decoded, djl.KeepAlive):
            numbers.setdefault(decoded.name, set()).add(decoded.device_number)
    assert numbers["CDJ-2000nexus"] <= {1, 2, 3, 4}
    assert numbers["DJM-2000nexus"] == {0x21}


@needs_dysentery
def test_linkinfo_capture_contains_the_dbserver_and_portmap_traffic():
    """Guards the assumption the dbserver work is built on.

    ``LinkInfo.pcapng`` is a CDJ using the LINK button to browse a peer, so it
    should show the 12523 port-discovery handshake, the dbserver conversation
    on 1051, and -- notably -- portmap traffic on UDP 111, which is the only
    published evidence of the NFS path being exercised alongside dbserver.
    """
    ports = Counter()
    for name in ("LinkInfo.pcapng", "LinkInfo2.pcapng"):
        path = DYSENTERY / name
        if path.exists():
            for packet in read_capture(path):
                ports[(packet.protocol, packet.dst_port)] += 1
    assert ports[("tcp", 1051)] > 0, "expected dbserver traffic"
    assert ports[("tcp", 12523)] > 0, "expected dbserver port discovery"
    assert ports[("udp", 111)] > 0, "expected portmap traffic"


@needs_libcdj
@pytest.mark.parametrize(
    "filename,expected_type",
    [
        ("cdj-keep-alive.dump", djl.PacketType.KEEP_ALIVE),
        ("cdj-discovery.dump", djl.PacketType.HELLO),
        ("cdj-stage1-discovery.dump", djl.PacketType.CLAIM_MAC),
        ("cdj-id-use-req.dump", djl.PacketType.CLAIM_IP),
        ("cdj-collision.dump", djl.PacketType.NUMBER_CONFLICT),
    ],
)
def test_libcdj_dumps_decode_and_round_trip(filename, expected_type):
    """A second, independent source of golden vectors.

    These are hand-annotated fixtures rather than live captures -- the MAC in
    the keep-alive is ``12:13:14:15:16:17``, plainly synthetic -- so the field
    *values* prove nothing. The *structure* is still an independent check on
    our offsets, from a different author than dysentery.
    """
    path = LIBCDJ_DUMPS / filename
    if not path.exists():
        pytest.skip(f"{filename} absent")
    raw = read_dump_file(path)
    decoded = djl.decode(raw)
    assert decoded.packet_type == expected_type
    assert decoded.encode() == raw


# -- RPC / NFS traffic in LinkInfo.pcapng ---------------------------------
#
# The single most consequential thing in these captures: a real player doing
# portmap -> MOUNT against two peers during a LINK session. It is direct
# evidence that CDJ-class hardware both runs and *uses* an NFS stack.


def _rpc_exchanges(capture: str = "LinkInfo.pcapng"):
    """Yield ``(call, reply)`` pairs of RPC traffic, matched by XID."""
    from prolinks_poc.proto import rpc

    path = DYSENTERY / capture
    if not path.exists():
        return
    calls: dict[int, object] = {}
    for packet in read_capture(path):
        if packet.protocol != "udp" or packet.dst_port in (50000, 50001, 50002, 5353, 67, 68):
            continue
        try:
            calls[rpc.parse_call(packet.payload).xid] = rpc.parse_call(packet.payload)
            continue
        except Exception:
            pass
        try:
            reply = rpc.parse_reply(packet.payload)
        except Exception:
            continue
        if reply.xid in calls:
            yield calls[reply.xid], reply


@needs_dysentery
def test_real_players_use_the_nfs_stack():
    """FINDINGS F5. ``research/06`` §1's NFS evidence was an XDJ capture; this
    is CDJ-class gear, and it settles that the RPC path is genuinely in use."""
    from prolinks_poc.proto import mountd, portmap

    programs = {call.program for call, _reply in _rpc_exchanges()}
    assert portmap.PROGRAM in programs, "expected portmap traffic"
    assert mountd.PROGRAM in programs, "expected MOUNT traffic"


@needs_dysentery
def test_observed_mountd_and_nfsd_ports():
    """FINDINGS F6: mountd on 48276, nfsd on the standard 2049."""
    from prolinks_poc.proto import mountd, nfs2, portmap
    from prolinks_poc.proto.xdr import XdrReader

    resolved: dict[int, set[int]] = {}
    for call, reply in _rpc_exchanges():
        if call.program != portmap.PROGRAM or call.procedure != portmap.Proc.GETPORT:
            continue
        program = XdrReader(call.args).u32()
        if reply.ok:
            resolved.setdefault(program, set()).add(
                portmap.decode_getport_result(reply.results)
            )
    assert resolved[mountd.PROGRAM] == {48276}
    assert resolved[nfs2.PROGRAM] == {2049}


@needs_dysentery
def test_auth_unix_stamp_is_random_per_call():
    """FINDINGS C8. ``research/06`` §2 calls the stamp "a magic constant the
    clients copy to look like a real CDJ". Every call in the capture carries a
    *different* stamp, so it is a nonce and its value is arbitrary."""
    from prolinks_poc.proto import rpc

    stamps = set()
    for call, _reply in _rpc_exchanges():
        assert call.cred_flavor == rpc.AuthFlavor.AUTH_UNIX
        credential = rpc.AuthUnix.decode(call.cred_body)
        assert (credential.machine_name, credential.uid, credential.gid) == ("", 0, 0)
        stamps.add(credential.stamp)
    assert len(stamps) > 5, "a fixed magic constant would give exactly one value"
    # prolink-connect's "observed CDJ value" is simply the first stamp here.
    assert rpc.STAMP_OBSERVED_CDJ in stamps


@needs_dysentery
def test_export_paths_vary_between_devices():
    """FINDINGS C6: ``/C/`` on one peer, ``/C/EXPORT`` on another, one session."""
    from prolinks_poc.proto import mountd
    from prolinks_poc.proto.xdr import XdrReader

    mounted = {
        XdrReader(call.args).string_utf16le()
        for call, _reply in _rpc_exchanges()
        if call.program == mountd.PROGRAM and call.procedure == mountd.Proc.MNT
    }
    assert mounted == {"/C/", "/C/EXPORT"}


@needs_dysentery
def test_export_listing_decodes_with_ascii_groups():
    """FINDINGS C7: the path is UTF-16LE but the group names are ASCII."""
    from prolinks_poc.proto import mountd

    listings = [
        mountd.decode_export_result(reply.results)
        for call, reply in _rpc_exchanges()
        if call.program == mountd.PROGRAM
        and call.procedure == mountd.Proc.EXPORT
        and reply.ok
    ]
    assert listings, "expected an EXPORT reply"
    exports = listings[0]
    assert all(export.path == "/C/EXPORT" for export in exports)
    # host/netmask pairs naming the peers allowed to mount.
    groups = {group for export in exports for group in export.groups}
    assert "169.254.244.181/255.255.255.255" in groups
    assert "169.254.192.112/255.255.255.255" in groups


@needs_dysentery
def test_mnt_replies_carry_32_byte_filehandles():
    from prolinks_poc.proto import mountd

    handles = [
        mountd.decode_mnt_result(reply.results)
        for call, reply in _rpc_exchanges()
        if call.program == mountd.PROGRAM
        and call.procedure == mountd.Proc.MNT
        and reply.ok
    ]
    assert handles
    assert all(len(handle) == 32 for handle in handles)


@needs_dysentery
def test_real_players_call_umnt():
    """FINDINGS C9: ``research/06`` lists UMNT as unused by clients."""
    from prolinks_poc.proto import mountd

    procedures = {
        call.procedure for call, _reply in _rpc_exchanges() if call.program == mountd.PROGRAM
    }
    assert mountd.Proc.UMNT in procedures


def test_match_export_handles_the_observed_variation():
    """The client must find USB whether it is spelled ``/C/`` or ``/C/EXPORT``."""
    from prolinks_poc.core.slots import MediaSlot, match_export

    assert match_export(["/C/"], MediaSlot.USB) == "/C/"
    assert match_export(["/C/EXPORT"], MediaSlot.USB) == "/C/EXPORT"
    assert match_export(["/B/", "/C/EXPORT"], MediaSlot.USB) == "/C/EXPORT"
    assert match_export(["/B/"], MediaSlot.USB) is None


def test_ip_fragment_reassembly():
    """A CDJ's 8192-byte NFS READ replies arrive as five or six IP fragments.

    Only the first carries a UDP header, so a reader that ignores fragments
    silently under-reports every transfer -- which is exactly what happened
    when first measuring how much audio crossed the wire (FINDINGS F18).
    """
    import struct

    from prolinks_poc.capture.pcap import _Defragmenter

    defrag = _Defragmenter()
    key = ("1.1.1.1", "2.2.2.2", 0x1234, 17)
    assert defrag.add(key, 0, b"A" * 1480, more=True) is None
    assert defrag.add(key, 1480, b"B" * 1480, more=True) is None
    assembled = defrag.add(key, 2960, b"C" * 100, more=False)
    assert assembled == b"A" * 1480 + b"B" * 1480 + b"C" * 100


def test_ip_fragment_reassembly_waits_for_a_hole():
    """An out-of-order arrival must not be mistaken for a complete datagram."""
    from prolinks_poc.capture.pcap import _Defragmenter

    defrag = _Defragmenter()
    key = ("1.1.1.1", "2.2.2.2", 1, 17)
    # Final fragment first: total is known, but the front is missing.
    assert defrag.add(key, 1480, b"B" * 100, more=False) is None
    assert defrag.add(key, 0, b"A" * 1480, more=True) == b"A" * 1480 + b"B" * 100
