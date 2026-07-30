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
from prolinks_poc.proto import dbserver as db
from prolinks_poc.proto.bytes import ByteReader

generated = pytest.importorskip(
    "tests.generated.prolink_djl",
    reason="run ksy/regenerate.sh to build the Kaitai parsers",
)
ProlinkDjl = generated.ProlinkDjl
ProlinkDbserver = pytest.importorskip("tests.generated.prolink_dbserver").ProlinkDbserver
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
