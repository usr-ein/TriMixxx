"""ONC RPC framing tests, both directions.

Encoding a call and decoding it with our own ``parse_call`` is what makes the
serve side cheap later; it also means a framing mistake shows up here rather
than as an unexplained timeout against hardware.
"""

from __future__ import annotations

import pytest

from prolinks_poc.proto import portmap, rpc
from prolinks_poc.proto.errors import DecodeError


def test_call_header_layout():
    call = rpc.build_call(
        0x11223344, portmap.PROGRAM, portmap.VERSION, portmap.Proc.GETPORT,
        portmap.encode_getport_args(100005, 1),
    )
    assert call[0:4] == bytes.fromhex("11223344")  # xid
    assert call[4:8] == b"\x00\x00\x00\x00"  # msg_type CALL
    assert call[8:12] == b"\x00\x00\x00\x02"  # rpcvers 2
    assert call[12:16] == (100000).to_bytes(4, "big")
    assert call[16:20] == (2).to_bytes(4, "big")
    assert call[20:24] == (3).to_bytes(4, "big")


def test_call_round_trip_with_auth_unix():
    credential = rpc.AuthUnix(stamp=rpc.STAMP_OBSERVED_CDJ, uid=0, gid=0)
    raw = rpc.build_call(1, 100003, 2, 6, b"\x01\x02\x03\x04", credential)
    parsed = rpc.parse_call(raw)

    assert parsed.xid == 1
    assert (parsed.program, parsed.version, parsed.procedure) == (100003, 2, 6)
    assert parsed.cred_flavor == rpc.AuthFlavor.AUTH_UNIX
    assert parsed.verf_flavor == rpc.AuthFlavor.AUTH_NULL
    assert parsed.args == b"\x01\x02\x03\x04"

    decoded = rpc.AuthUnix.decode(parsed.cred_body)
    assert decoded.stamp == rpc.STAMP_OBSERVED_CDJ
    assert decoded.uid == 0


def test_auth_null_produces_an_empty_credential_body():
    """Experiment E2's H1: libcdj's default was AUTH_NULL, and it failed."""
    raw = rpc.build_call(1, 100005, 1, 1, b"", rpc.AUTH_NULL_CRED)
    parsed = rpc.parse_call(raw)
    assert parsed.cred_flavor == rpc.AuthFlavor.AUTH_NULL
    assert parsed.cred_body == b""


def test_both_documented_stamps_are_available():
    assert rpc.STAMP_OBSERVED_CDJ == 0x967B8703
    assert rpc.STAMP_DEADBEEF == 0xDEADBEEF


def test_accepted_reply_round_trip():
    raw = rpc.build_reply(0xABCD, b"\xde\xad\xbe\xef")
    reply = rpc.parse_reply(raw)
    assert reply.xid == 0xABCD
    assert reply.ok
    assert reply.results == b"\xde\xad\xbe\xef"


def test_error_reply_raises_with_a_named_status():
    raw = rpc.build_reply(1, b"", rpc.AcceptStat.PROC_UNAVAIL)
    reply = rpc.parse_reply(raw)
    assert not reply.ok
    with pytest.raises(rpc.RpcCallFailed, match="PROC_UNAVAIL"):
        reply.raise_for_status("READDIR")


def test_prog_mismatch_carries_the_supported_version_range():
    """'Wrong version' and 'not there' are different findings for E4."""
    raw = rpc.build_reply(1, b"", rpc.AcceptStat.PROG_MISMATCH, low_version=2, high_version=3)
    reply = rpc.parse_reply(raw)
    assert (reply.low_version, reply.high_version) == (2, 3)
    with pytest.raises(rpc.RpcCallFailed, match="versions 2..3"):
        reply.raise_for_status()


def test_denied_reply():
    raw = rpc.build_denied_reply(1, rpc.RejectStat.AUTH_ERROR)
    reply = rpc.parse_reply(raw)
    assert not reply.accepted
    with pytest.raises(rpc.RpcCallFailed, match="AUTH_ERROR"):
        reply.raise_for_status()


def test_a_call_is_not_mistaken_for_a_reply():
    raw = rpc.build_call(1, 100000, 2, 0)
    with pytest.raises(DecodeError, match="expected an RPC REPLY"):
        rpc.parse_reply(raw)


def test_portmap_getport_codec():
    args = portmap.encode_getport_args(100003, 2, portmap.IPPROTO_UDP)
    assert args == (
        (100003).to_bytes(4, "big") + (2).to_bytes(4, "big")
        + (17).to_bytes(4, "big") + (0).to_bytes(4, "big")
    )
    assert portmap.decode_getport_result((2049).to_bytes(4, "big")) == 2049


def test_portmap_getport_zero_means_not_registered():
    assert portmap.decode_getport_result(b"\x00\x00\x00\x00") == 0


def test_portmap_dump_round_trip():
    mappings = [
        portmap.Mapping(100000, 2, portmap.IPPROTO_UDP, 111),
        portmap.Mapping(100005, 1, portmap.IPPROTO_UDP, 48276),
        portmap.Mapping(100003, 2, portmap.IPPROTO_UDP, 2049),
    ]
    decoded = portmap.decode_dump_result(portmap.encode_dump_result(mappings))
    assert decoded == mappings
    assert decoded[2].program_name == "nfs"
    assert decoded[1].protocol_name == "udp"
