"""DJ-Link codec tests, checked against the tables in ``research/02``.

The round-trip assertions are the load-bearing ones: because every packet type
implements both directions, ``encode(decode(x)) == x`` catches an offset
mistake in either half. When real captures land in ``fixtures/``, add them here
as hex literals -- a decoder that agrees with our own encoder but not with the
hardware is exactly the failure these tests cannot see on their own.
"""

from __future__ import annotations

import pytest

from prolinks_poc.proto import djl
from prolinks_poc.proto.errors import DecodeError

NAME = "CDJ-2000nexus"
MAC = bytes.fromhex("aabbccddeeff")
IP = "169.254.119.181"


def test_magic_is_the_documented_ten_bytes():
    assert djl.MAGIC == bytes.fromhex("5173707431576d4a4f4c")
    assert djl.MAGIC == b"Qspt1WmJOL"
    assert len(djl.MAGIC) == 10


def test_keepalive_matches_the_documented_layout():
    """research/02 §2: 0x36 bytes, with each field at a stated offset."""
    packet = djl.KeepAlive(
        name=NAME, name_raw=b"", device_kind=djl.DeviceKind.CDJ,
        device_number=2, mac=MAC, ip=IP, peer_count=3,
    )
    raw = packet.encode()

    assert len(raw) == 0x36
    assert raw[0x00:0x0A] == djl.MAGIC
    assert raw[0x0A] == 0x06  # type
    assert raw[0x0B] == 0x00  # subtype
    assert raw[0x0C:0x20] == NAME.encode().ljust(20, b"\x00")
    assert raw[0x20] == 0x01  # constant
    assert raw[0x21] == 0x02  # device kind: CDJ
    assert raw[0x22] == 0x00  # padding
    assert raw[0x23] == 0x36  # stype == length for this type
    assert raw[0x24] == 2  # D
    assert raw[0x25] == 0x01
    assert raw[0x26:0x2C] == MAC
    assert raw[0x2C:0x30] == bytes([169, 254, 119, 181])
    assert raw[0x30] == 3  # peer count
    assert raw[0x31:0x34] == b"\x00\x00\x00"
    assert raw[0x34] == 0x01  # role: CDJ
    # research/02 §2 calls 0x01 "typical", but every nexus keep-alive in the
    # dysentery captures carries 0x00. FINDINGS.md C3; see test_captures.py.
    assert raw[0x35] == 0x00


def test_keepalive_trailing_byte_distinguishes_cdj3000():
    """research/02 §1.6: byte 0x35 is 0x64 for CDJ-3000 compatibility.

    Getting this wrong "can cause CDJ-3000s set to player 5/6 to repeatedly
    kick themselves off the network", so it is worth an explicit test.
    """
    packet = djl.KeepAlive(
        name=NAME, name_raw=b"", device_kind=djl.DeviceKind.REKORDBOX_OR_CDJ3000,
        device_number=5, mac=MAC, ip=IP, trailing=0x64,
    )
    raw = packet.encode()
    assert raw[0x35] == 0x64
    assert djl.decode(raw).trailing == 0x64


def test_name_field_is_truncated_and_nul_padded():
    packet = djl.KeepAlive(
        name="an-extremely-long-device-name-well-over-twenty",
        name_raw=b"", device_kind=djl.DeviceKind.CDJ,
        device_number=1, mac=MAC, ip=IP,
    )
    raw = packet.encode()
    assert len(raw[0x0C:0x20]) == 20
    assert raw[0x0C:0x20] == b"an-extremely-long-de"


def test_decode_preserves_the_literal_name_bytes():
    """Milestone M1's whole point: the exact bytes, not our reading of them."""
    raw = bytearray(
        djl.KeepAlive(
            name=NAME, name_raw=b"", device_kind=djl.DeviceKind.CDJ,
            device_number=1, mac=MAC, ip=IP,
        ).encode()
    )
    decoded = djl.decode(bytes(raw))
    assert decoded.name_raw == NAME.encode().ljust(20, b"\x00")
    assert decoded.name == NAME


def test_decode_survives_a_non_ascii_name():
    """A stray byte in a cosmetic field must not cost us the whole packet."""
    raw = bytearray(
        djl.KeepAlive(
            name=NAME, name_raw=b"", device_kind=djl.DeviceKind.CDJ,
            device_number=1, mac=MAC, ip=IP,
        ).encode()
    )
    raw[0x0C] = 0xFF
    decoded = djl.decode(bytes(raw))
    assert decoded.name_raw[0] == 0xFF
    assert decoded.device_number == 1


@pytest.mark.parametrize(
    "packet",
    [
        djl.Hello(name=NAME, name_raw=b"", device_kind=djl.DeviceKind.CDJ, payload=0x01),
        djl.ClaimMac(
            name=NAME, name_raw=b"", device_kind=djl.DeviceKind.CDJ,
            iteration=2, flags=0x01, mac=MAC,
        ),
        djl.ClaimIp(
            name=NAME, name_raw=b"", device_kind=djl.DeviceKind.CDJ,
            ip=IP, mac=MAC, device_number=3, iteration=1,
            assignment_mode=djl.AssignmentMode.AUTO,
        ),
        djl.ClaimNumber(
            name=NAME, name_raw=b"", device_kind=djl.DeviceKind.CDJ,
            device_number=3, iteration=1,
        ),
        djl.KeepAlive(
            name=NAME, name_raw=b"", device_kind=djl.DeviceKind.CDJ,
            device_number=2, mac=MAC, ip=IP, peer_count=2,
        ),
        djl.NumberConflict(
            name=NAME, name_raw=b"", device_kind=djl.DeviceKind.CDJ,
            device_number=3, ip=IP,
        ),
    ],
    ids=["hello", "claim_mac", "claim_ip", "claim_number", "keepalive", "conflict"],
)
def test_round_trip(packet):
    raw = packet.encode()
    decoded = djl.decode(raw)
    assert type(decoded) is type(packet)
    assert decoded.encode() == raw


def test_packet_lengths():
    """research/02 §0.1's length column, corrected for type 04 (FINDINGS C2)."""
    expected = {
        djl.PacketType.HELLO: 0x25,
        djl.PacketType.CLAIM_MAC: 0x2C,
        djl.PacketType.CLAIM_IP: 0x32,
        djl.PacketType.CLAIM_NUMBER: 0x26,
        djl.PacketType.KEEP_ALIVE: 0x36,
        djl.PacketType.NUMBER_CONFLICT: 0x29,
    }
    assert djl._LENGTH == expected


def test_stype_always_equals_the_packet_length():
    """True for every type once type 04's documented length is corrected."""
    for packet_type, length in djl._LENGTH.items():
        assert djl._STYPE[packet_type] == length, f"{packet_type!r} disagrees"


def test_claim_number_is_38_bytes():
    packet = djl.ClaimNumber(
        name=NAME, name_raw=b"", device_kind=djl.DeviceKind.CDJ,
        device_number=3, iteration=1,
    )
    raw = packet.encode()
    assert len(raw) == 0x26
    assert raw[0x23] == 0x26  # stype == length
    assert (raw[0x24], raw[0x25]) == (3, 1)


def test_role_byte_tracks_the_device_kind():
    """FINDINGS C1: byte 0x30 of the stage-2 claim is a role, not a constant."""
    assert djl.default_role(djl.DeviceKind.CDJ) == 0x01
    assert djl.default_role(djl.DeviceKind.MIXER) == 0x02

    mixer = djl.ClaimIp(
        name="DJM-2000nexus", name_raw=b"", device_kind=djl.DeviceKind.MIXER,
        ip=IP, mac=MAC, device_number=0x21, iteration=1,
    )
    assert mixer.encode()[0x30] == 0x02

    player = djl.ClaimIp(
        name=NAME, name_raw=b"", device_kind=djl.DeviceKind.CDJ,
        ip=IP, mac=MAC, device_number=3, iteration=1,
    )
    assert player.encode()[0x30] == 0x01


def test_unknown_type_becomes_data_not_an_error():
    """Mixer-side types must land in the journal rather than vanish."""
    raw = bytearray(
        djl.KeepAlive(
            name=NAME, name_raw=b"", device_kind=djl.DeviceKind.CDJ,
            device_number=1, mac=MAC, ip=IP,
        ).encode()
    )
    raw[0x0A] = 0x03  # mixer "use device number D"
    decoded = djl.decode(bytes(raw))
    assert isinstance(decoded, djl.UnknownPacket)
    assert decoded.raw_type == 0x03


def test_rejects_foreign_traffic():
    with pytest.raises(DecodeError, match="not a DJ-Link packet"):
        djl.decode(b"GET / HTTP/1.1\r\n\r\n")


def test_rejects_a_truncated_packet():
    full = djl.KeepAlive(
        name=NAME, name_raw=b"", device_kind=djl.DeviceKind.CDJ,
        device_number=1, mac=MAC, ip=IP,
    ).encode()
    with pytest.raises(DecodeError):
        djl.decode(full[:0x30])


def test_is_djl_packet_guard():
    assert djl.is_djl_packet(djl.MAGIC + b"\x06")
    assert not djl.is_djl_packet(b"short")
    assert not djl.is_djl_packet(b"X" * 40)


# -- golden vector from our own CDJ-2000NXS ---------------------------------
#
# Captured 2026-07-29 from the author's own unit (S01-cold-boot-a). Unlike the
# dysentery captures this is ours, so it can live in the repository rather than
# being read out of a git-ignored clone -- which makes it the one golden vector
# guaranteed to be available wherever these tests run.

NXS_KEEPALIVE = bytes.fromhex(
    "5173707431576d4a4f4c060043444a2d323030306e657875730000000000"
    "0000010200360102745e1c5667aca9fe67ac010000000100"
)


def test_real_nxs_keepalive_decodes():
    packet = djl.decode(NXS_KEEPALIVE)
    assert isinstance(packet, djl.KeepAlive)
    assert packet.name == "CDJ-2000nexus"
    assert packet.device_kind == djl.DeviceKind.CDJ
    assert packet.device_number == 1
    assert packet.mac == bytes.fromhex("745e1c5667ac")
    assert packet.ip == "169.254.103.172"
    assert packet.peer_count == 1
    # The three bytes whose meaning the research docs get wrong or leave open.
    assert packet.const_25 == 0x02   # alone on the network; see FINDINGS O3
    assert packet.flags == 0x01      # role: CDJ
    assert packet.trailing == 0x00   # not 0x01 as documented; FINDINGS C3


def test_real_nxs_keepalive_round_trips():
    assert djl.decode(NXS_KEEPALIVE).encode() == NXS_KEEPALIVE


def test_our_announcer_matches_the_real_keepalive_except_for_identity():
    """Everything we synthesise should equal a real CDJ's, bar identity fields.

    The differing bytes must be exactly: device number, MAC, IP, peer count,
    and byte 0x25 (meaning still unresolved). Anything else drifting means the
    impersonation has regressed.
    """
    ours = djl.KeepAlive(
        name="CDJ-2000nexus", name_raw=b"", device_kind=djl.DeviceKind.CDJ,
        device_number=1, mac=bytes.fromhex("745e1c5667ac"),
        ip="169.254.103.172", peer_count=1, const_25=0x02,
    ).encode()
    assert ours == NXS_KEEPALIVE
