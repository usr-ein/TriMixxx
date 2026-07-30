"""The ``.ksy`` schemas, checked against every packet we have ever captured.

This is the contract for the C++ port. The same ``.ksy`` compiles to the parsers
Mixxx ships and to the Python ones imported here, so a field that decodes
correctly in this test decodes correctly in Mixxx -- and any disagreement with
``prolinks_poc.proto``, which was written independently and validated against
hardware over ~15 capture sessions, means one of the two is wrong.

Two implementations agreeing is weak evidence; the value is in having a *third*
opinion on bytes that have already cost us five wrong inferences (F26/F40,
F27/F41, F31/F34/F35, F29/F30, O6).

Skipped rather than failed when the generated Python is absent, because it is
build output: run ``ksy/regenerate.sh``. It is *not* skipped when the capture
corpus is missing -- there is a committed fixture floor below, so a coverage
regression cannot hide behind an empty corpus.
"""

from __future__ import annotations

import glob
import io
import json
import socket
from pathlib import Path

import pytest

from prolinks_poc.capture.pcap import read_capture, tcp_streams
from prolinks_poc.proto import djl
from prolinks_poc.proto import djl_status as status
from prolinks_poc.proto import dbserver as db
from prolinks_poc.proto import mountd, nfs2, portmap, rpc
from prolinks_poc.proto.bytes import ByteReader
from prolinks_poc.proto.errors import DecodeError
from prolinks_poc.proto.xdr import XdrReader

generated = pytest.importorskip(
    "tests.generated.prolink_djl",
    reason="run ksy/regenerate.sh to build the Kaitai parsers",
)
ProlinkDjl = generated.ProlinkDjl
ProlinkDbserver = pytest.importorskip("tests.generated.prolink_dbserver").ProlinkDbserver
ProlinkStatus = pytest.importorskip("tests.generated.prolink_status").ProlinkStatus
ProlinkRpc = pytest.importorskip("tests.generated.prolink_rpc").ProlinkRpc
KaitaiStream = pytest.importorskip("kaitaistruct").KaitaiStream

ROOT = Path(__file__).resolve().parent.parent

#: A real 54-byte keep-alive, byte for byte, from captures/S24b-e9-control.
#: Committed so this file still tests something on a machine with no captures --
#: and so the header layout is pinned by a literal rather than by whatever
#: happens to be in captures/ today.
KEEP_ALIVE = bytes.fromhex(
    "5173707431576d4a4f4c060043444a2d3230303"
    "06e6578757300000000000000010200360502a0"
    "cec8e226dea9fe6364010000000100"
)


def _djl_payloads():
    """Every UDP-50000 payload in the corpus: journals first, then pcaps."""
    for path in sorted(glob.glob(str(ROOT / "captures/journals/*/journal.jsonl"))):
        for line in open(path):
            if '"local_port": 50000' not in line:
                continue
            record = json.loads(line)
            if record.get("hex"):
                yield bytes.fromhex(record["hex"])
    patterns = ("captures/S*/run.pcap", "research/ref-repos/dysentery/doc/assets/*.pcapng")
    for pattern in patterns:
        for path in sorted(glob.glob(str(ROOT / pattern))):
            try:
                packets = list(read_capture(path))
            except Exception:  # a truncated or unreadable capture costs that file
                continue
            for packet in packets:
                if 50000 not in (packet.src_port, packet.dst_port):
                    continue
                if packet.payload.startswith(b"Qspt1WmJOL"):
                    yield packet.payload


def test_the_committed_keep_alive_decodes():
    """No corpus needed. Pins the common header and the keep-alive body."""
    packet = ProlinkDjl.from_bytes(KEEP_ALIVE)
    assert packet.packet_type == ProlinkDjl.PacketType.keep_alive
    assert packet.device_name == "CDJ-2000nexus"
    assert packet.device_name_raw == b"CDJ-2000nexus" + b"\0" * 7
    assert packet.device_kind == ProlinkDjl.DeviceKind.cdj
    # stype equals the datagram length on every type we have seen (C2).
    assert packet.stype == len(KEEP_ALIVE) == 0x36
    assert packet.body.device_number == 5
    assert packet.body.mac.hex() == "a0cec8e226de"
    assert socket.inet_ntoa(packet.body.ip) == "169.254.99.100"
    assert packet.body.peer_count == 1
    # 0x00 on nexus hardware, not 0x01 as research/02 has it (C3).
    assert packet.body.trailing == 0x00


def test_ksy_agrees_with_the_hand_written_decoder_across_the_corpus():
    """Same bytes into two independent implementations, field by field."""
    scalars = ("device_number", "iteration", "peer_count", "assignment_mode", "role")
    seen, disagreements = 0, []

    for raw in _djl_payloads():
        seen += 1
        packet = ProlinkDjl.from_bytes(raw)  # must not raise, for any capture
        try:
            reference = djl.decode(raw)
        except Exception:
            continue  # a type the PoC does not model; Kaitai parsing it is enough

        if getattr(reference, "name", None) not in (None, packet.device_name):
            disagreements.append(("name", packet.device_name, reference.name))
        for field in scalars:
            mine = getattr(packet.body, field, None)
            theirs = getattr(reference, field, None)
            if mine is None or theirs is None:
                continue
            mine = mine.value if hasattr(mine, "value") else mine
            if int(mine) != int(theirs):
                disagreements.append((field, mine, theirs))
        if getattr(reference, "mac", None) and getattr(packet.body, "mac", None):
            if packet.body.mac != reference.mac:
                disagreements.append(("mac", packet.body.mac.hex(), reference.mac.hex()))
        if getattr(reference, "ip", None) and getattr(packet.body, "ip", None):
            if socket.inet_ntoa(packet.body.ip) != reference.ip:
                disagreements.append(("ip", socket.inet_ntoa(packet.body.ip), reference.ip))

    assert not disagreements, f"{len(disagreements)} field disagreements: {disagreements[:8]}"
    assert seen >= 1, "no UDP-50000 payloads found at all -- is the corpus present?"


def _dbserver_streams():
    """Every reassembled TCP-1051 byte stream in the corpus, both directions."""
    patterns = (
        "research/ref-repos/dysentery/doc/assets/LinkInfo*.pcapng",
        "captures/S*/run.pcap",
    )
    for pattern in patterns:
        for path in sorted(glob.glob(str(ROOT / pattern))):
            try:
                packets = list(read_capture(path))
            except Exception:  # a truncated or unreadable capture costs that file
                continue
            yield from tcp_streams(packets, ports={db.DEFAULT_DBSERVER_PORT}).values()


def test_dbserver_ksy_agrees_with_the_hand_written_decoder():
    """The dbserver schema against every captured message, field by field.

    Worth more here than for the discovery packets, because this is the format
    ``research/10`` predicted would fight: two independent tag numberings that
    must agree, and a zero-length binary argument that is **omitted from the wire
    entirely**. Both are silent when wrong -- a parser that mishandles the second
    reads the next message's magic as a field and every argument after that is one
    position out, with nothing to show for it.

    Framing is checked as well as content: ``pos()`` after the parse must equal
    what the hand-written decoder consumed, which is the only way a desynchronised
    reader would show up.
    """
    seen, disagreements = 0, []

    for data in _dbserver_streams():
        start = len(db.PREAMBLE) if data.startswith(db.PREAMBLE) else 0
        reader = ByteReader(data, start)
        while not reader.at_end():
            before = reader.pos
            try:
                reference = db.decode_message(reader)
            except Exception:
                break  # a partial trailing message; normal at a capture boundary

            stream = KaitaiStream(io.BytesIO(data[before:]))
            parsed = ProlinkDbserver(stream)  # must not raise, for any capture
            seen += 1

            checks = (
                ("consumed", stream.pos(), reader.pos - before),
                ("transaction_id", parsed.transaction_id, reference.transaction_id),
                ("type", parsed.message_type, reference.type),
                ("num_args", len(parsed.args), len(reference.args)),
            )
            for name, mine, theirs in checks:
                if mine != theirs:
                    disagreements.append((name, mine, theirs, reference.type_name))

            for index, argument in enumerate(parsed.args):
                theirs = reference.args[index]
                if not argument.is_present:
                    mine = b""
                elif argument.field.field_type == db.FieldType.BINARY:
                    mine = argument.field.blob
                elif argument.field.field_type == db.FieldType.STRING:
                    mine = argument.field.text_raw.decode("utf-16-be").rstrip("\x00")
                else:
                    mine = argument.field.num_value
                if mine != theirs:
                    disagreements.append((f"arg{index}", mine, theirs, reference.type_name))

    assert not disagreements, f"{len(disagreements)} disagreements: {disagreements[:8]}"
    if seen == 0:
        pytest.skip("no dbserver traffic in the corpus on this machine")
    # The dysentery LinkInfo captures alone hold well over ten thousand.
    assert seen > 200


def _udp_payloads(port: int):
    """Every UDP payload **addressed to** *port* in the corpus.

    The destination, not either endpoint, and that distinction is the whole
    reason this helper is not one line. The type byte at 0x0a is shared across
    ports and the layouts behind it are not: `0x06` is a keep-alive on 50000 and
    a media response on 50002. A tool that binds one socket and sends its
    keep-alives *from* 50002 therefore contributes packets that match on
    `src_port` and decode, under this schema, into confident nonsense — which is
    exactly the failure `prolink_status.ksy` is kept separate to avoid, and which
    a `port in (src, dst)` filter walked straight into.
    """
    for path in sorted(glob.glob(str(ROOT / "captures/journals/*/journal.jsonl"))):
        for line in open(path):
            if f'"port": {port}' not in line:
                continue
            record = json.loads(line)
            if record.get("hex"):
                yield bytes.fromhex(record["hex"])
    patterns = ("captures/S*/run.pcap", "research/ref-repos/dysentery/doc/assets/*.pcapng")
    for pattern in patterns:
        for path in sorted(glob.glob(str(ROOT / pattern))):
            try:
                packets = list(read_capture(path))
            except Exception:  # a truncated or unreadable capture costs that file
                continue
            for packet in packets:
                if packet.dst_port == port and packet.payload:
                    yield packet.payload


def _enum_value(field):
    """The integer behind a Kaitai enum field.

    The Python target hands back a bare ``int`` when the value is not one the
    enum declares, and an enum member when it is. Both happen here: a media state
    of 0x02 is declared, and slot bytes outside 0-4 turn up in the corpus. The
    C++ target simply casts, so this asymmetry is the Python binding's alone.
    """
    return field.value if hasattr(field, "value") else field


def test_status_ksy_agrees_with_the_hand_written_decoder():
    """The UDP-50002 schema, field by field, against every captured packet.

    Both halves matter for the serve side. We *read* status packets to learn who
    holds tempo master, and we *answer* the media and settings queries -- and
    until those were answered a deck that had otherwise fully accepted us still
    refused to list us as a source, because as far as it knew our slots held
    nothing (F24).
    """
    seen, kinds, disagreements = 0, set(), []

    for raw in _udp_payloads(status.STATUS_PORT):
        if not raw.startswith(djl.MAGIC):
            continue
        try:
            packet = ProlinkStatus.from_bytes(raw)
        except Exception as exc:  # noqa: BLE001 -- the assertion is the report
            disagreements.append(("parse", repr(exc), raw[:16].hex()))
            continue
        seen += 1
        kinds.add(packet.packet_type.name)

        if packet.packet_type == ProlinkStatus.PacketType.cdj_status:
            try:
                reference = status.decode_status(raw)
            except DecodeError:
                continue  # shorter than the media fields; nothing to compare
            for name, mine, theirs in (
                ("name", packet.device_name, reference.name),
                ("device", packet.sender_device, reference.device_number),
                ("usb", _enum_value(packet.status_usb_state), reference.usb_state),
                ("sd", _enum_value(packet.status_sd_state), reference.sd_state),
                ("link", packet.status_link_available, reference.link_available),
                ("track_id", packet.status_track_id, reference.track_id),
            ):
                if mine != theirs:
                    disagreements.append((name, mine, theirs, "cdj_status"))

        elif packet.packet_type == ProlinkStatus.PacketType.media_query:
            reference = status.decode_media_query(raw)
            mine_ip = ".".join(str(b) for b in packet.query_requester_ip)
            for name, mine, theirs in (
                ("requester", packet.sender_device, reference.requester),
                ("requester_ip", mine_ip, reference.requester_ip),
                ("target", packet.query_target_device, reference.target_device),
                ("slot", _enum_value(packet.query_slot), reference.slot),
            ):
                if mine != theirs:
                    disagreements.append((name, mine, theirs, "media_query"))

        elif packet.packet_type == ProlinkStatus.PacketType.settings_query:
            reference = status.decode_settings_query(raw)
            for name, mine, theirs in (
                ("requester", packet.settings_requester, reference.requester),
                ("sender", packet.sender_device, reference.sender),
                ("slot", _enum_value(packet.settings_slot), reference.slot),
            ):
                if mine != theirs:
                    disagreements.append((name, mine, theirs, "settings_query"))

    assert not disagreements, f"{len(disagreements)} disagreements: {disagreements[:8]}"
    if seen == 0:
        pytest.skip("no UDP-50002 traffic in the corpus on this machine")
    assert "cdj_status" in kinds, f"only saw {sorted(kinds)}"


#: Which Kaitai argument type each (program, procedure) should decode to, and how
#: to check it against the hand-written codec. Only the calls a CDJ actually
#: makes -- the ones our server has to answer.
_RPC_EXPECTED = {
    (portmap.PROGRAM, portmap.Proc.GETPORT): "getport_args",
    (mountd.PROGRAM, mountd.Proc.MNT): "path_args",
    (nfs2.PROGRAM, nfs2.Proc.LOOKUP): "lookup_args",
    (nfs2.PROGRAM, nfs2.Proc.READ): "read_args",
    (nfs2.PROGRAM, nfs2.Proc.GETATTR): "fhandle_args",
    (nfs2.PROGRAM, nfs2.Proc.STATFS): "fhandle_args",
}


def test_rpc_ksy_agrees_with_the_hand_written_decoder():
    """The RPC call schema against every portmap/mountd/nfsd call in the corpus.

    These are the calls a real player will make *to us*, so the schema is the
    serve side's front door. Two Pioneer deviations are what it has to get right:
    path and file names are **UTF-16LE counted in bytes**, not the ASCII standard
    NFS uses, and the credential is AUTH_UNIX with a fresh stamp per call rather
    than the magic constant documentation claimed (C8).
    """
    seen, procedures, disagreements = 0, set(), []
    ports = (portmap.PORT, mountd.PIONEER_PORT, nfs2.PORT)

    for port in ports:
        for raw in _udp_payloads(port):
            try:
                reference = rpc.parse_call(raw)
            except DecodeError:
                continue  # a reply, or another program's traffic on this port
            try:
                call = ProlinkRpc.from_bytes(raw)
            except Exception as exc:  # noqa: BLE001
                disagreements.append(("parse", repr(exc), raw[:24].hex()))
                continue
            seen += 1
            procedures.add((reference.program, reference.procedure))

            for name, mine, theirs in (
                ("xid", call.xid, reference.xid),
                ("program", _enum_value(call.program), reference.program),
                ("version", call.program_version, reference.version),
                ("procedure", call.procedure, reference.procedure),
                ("cred_flavor", _enum_value(call.credential.flavor), reference.cred_flavor),
                ("cred_body", call.credential.body, reference.cred_body),
            ):
                if mine != theirs:
                    disagreements.append((name, mine, theirs, reference.procedure))

            wanted = _RPC_EXPECTED.get((reference.program, reference.procedure))
            if wanted is None:
                continue
            kind = type(call.arguments).__name__
            if kind.lower() != "".join(part.title() for part in wanted.split("_")).lower():
                disagreements.append(("args_type", kind, wanted, reference.procedure))
                continue

            # The one field worth cross-checking in full: a mangled name is the
            # difference between a track that loads and NFSERR_NOENT.
            if wanted == "lookup_args":
                reader = XdrReader(reference.args)
                reader.opaque_fixed(nfs2.FHANDLE_SIZE)
                mine = call.arguments.name.value.decode("utf-16-le")
                theirs = reader.string_utf16le()
                if mine != theirs:
                    disagreements.append(("lookup_name", mine, theirs, "LOOKUP"))
            elif wanted == "path_args":
                mine = call.arguments.path.value.decode("utf-16-le")
                theirs = XdrReader(reference.args).string_utf16le()
                if mine != theirs:
                    disagreements.append(("mnt_path", mine, theirs, "MNT"))

    assert not disagreements, f"{len(disagreements)} disagreements: {disagreements[:8]}"
    if seen == 0:
        pytest.skip("no RPC traffic in the corpus on this machine")


def test_every_packet_type_we_have_ever_seen_is_covered():
    """A coverage floor, so a schema change cannot quietly stop handling a type.

    The eight below are what 38 capture files contain. ``number_conflict``
    (0x08) is deliberately absent: no capture holds one, because nothing has
    ever contested a number on this rig. That gap is the point of writing it
    down -- our conflict handling is untested against hardware.
    """
    types = {ProlinkDjl.from_bytes(raw).packet_type.name for raw in _djl_payloads()}
    if not types:
        pytest.skip("no capture corpus on this machine")
    expected = {
        "hello", "claim_mac", "claim_ip", "claim_number",
        "keep_alive", "number_in_use", "mixer_assign_intent", "mixer_assign",
    }
    assert expected <= types, f"stopped decoding: {sorted(expected - types)}"
    assert "number_conflict" not in types, (
        "a capture now contains a type-0x08 conflict -- promote it to a pinned "
        "fixture and test the announcer's back-off against it"
    )
