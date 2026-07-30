# This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild
# type: ignore

import kaitaistruct
from kaitaistruct import KaitaiStruct, KaitaiStream, BytesIO
from enum import IntEnum


if getattr(kaitaistruct, 'API_VERSION', (0, 9)) < (0, 11):
    raise Exception("Incompatible Kaitai Struct Python API: 0.11 or later is required, but you have %s" % (kaitaistruct.__version__))

class ProlinkDjl(KaitaiStruct):
    """The broadcast announcement protocol Pioneer players and mixers use to find
    each other and to agree on device numbers, as observed on two CDJ-2000NXS
    running firmware 1.44.
    
    Written from `docs/PROTOCOL.md` §2 and the evidence in `docs/FINDINGS.md`,
    both of which come from our own captures of our own hardware. It is not
    derived from any other project's schema.
    
    **This describes UDP 50000 only.** Port 50002 carries a different header --
    the name starts at 0x0b rather than 0x0c and byte 0x1f is a structural 0x01
    (C14) -- and lives in `prolink_status.ksy`. Sharing one parser between them
    yields plausible nonsense rather than an error, which is worse.
    
    The handshake is:
    
        3x hello -> 3x claim_mac -> 3x claim_ip -> Nx claim_number -> keep_alive forever
    
    ~300 ms apart, all broadcast. N is 3 into an empty network and 1 into a
    populated one (C13) -- it is *not* governed by the auto/manual setting, which
    is what `research/02` §1.0 claims.
    
    .. seealso::
       docs/PROTOCOL.md
    """

    class AssignmentMode(IntEnum):
        auto = 1
        manual = 2

    class DeviceKind(IntEnum):
        mixer = 1
        cdj = 2
        rekordbox_or_cdj3000 = 3
        cdj3000_hello = 4

    class PacketType(IntEnum):
        claim_mac = 0
        mixer_assign_intent = 1
        claim_ip = 2
        mixer_assign = 3
        claim_number = 4
        number_in_use = 5
        keep_alive = 6
        number_conflict = 8
        hello = 10
    def __init__(self, _io, _parent=None, _root=None):
        super(ProlinkDjl, self).__init__(_io)
        self._parent = _parent
        self._root = _root or self
        self._read()

    def _read(self):
        self.magic = self._io.read_bytes(10)
        if not self.magic == b"\x51\x73\x70\x74\x31\x57\x6D\x4A\x4F\x4C":
            raise kaitaistruct.ValidationNotEqualError(b"\x51\x73\x70\x74\x31\x57\x6D\x4A\x4F\x4C", self.magic, self._io, u"/seq/0")
        self.packet_type = KaitaiStream.resolve_enum(ProlinkDjl.PacketType, self._io.read_u1())
        self.subtype = self._io.read_u1()
        self.device_name = (KaitaiStream.bytes_terminate(self._io.read_bytes(20), 0, False)).decode(u"ASCII")
        self.const_one = self._io.read_bytes(1)
        if not self.const_one == b"\x01":
            raise kaitaistruct.ValidationNotEqualError(b"\x01", self.const_one, self._io, u"/seq/4")
        self.device_kind = KaitaiStream.resolve_enum(ProlinkDjl.DeviceKind, self._io.read_u1())
        self.pad_22 = self._io.read_bytes(1)
        if not self.pad_22 == b"\x00":
            raise kaitaistruct.ValidationNotEqualError(b"\x00", self.pad_22, self._io, u"/seq/6")
        self.stype = self._io.read_u1()
        _on = self.packet_type
        if _on == ProlinkDjl.PacketType.claim_ip:
            pass
            self.body = ProlinkDjl.ClaimIpBody(self._io, self, self._root)
        elif _on == ProlinkDjl.PacketType.claim_mac:
            pass
            self.body = ProlinkDjl.ClaimMacBody(self._io, self, self._root)
        elif _on == ProlinkDjl.PacketType.claim_number:
            pass
            self.body = ProlinkDjl.NumberBody(self._io, self, self._root)
        elif _on == ProlinkDjl.PacketType.hello:
            pass
            self.body = ProlinkDjl.HelloBody(self._io, self, self._root)
        elif _on == ProlinkDjl.PacketType.keep_alive:
            pass
            self.body = ProlinkDjl.KeepAliveBody(self._io, self, self._root)
        elif _on == ProlinkDjl.PacketType.number_conflict:
            pass
            self.body = ProlinkDjl.NumberConflictBody(self._io, self, self._root)
        elif _on == ProlinkDjl.PacketType.number_in_use:
            pass
            self.body = ProlinkDjl.NumberBody(self._io, self, self._root)
        else:
            pass
            self.body = ProlinkDjl.UnknownBody(self._io, self, self._root)


    def _fetch_instances(self):
        pass
        _on = self.packet_type
        if _on == ProlinkDjl.PacketType.claim_ip:
            pass
            self.body._fetch_instances()
        elif _on == ProlinkDjl.PacketType.claim_mac:
            pass
            self.body._fetch_instances()
        elif _on == ProlinkDjl.PacketType.claim_number:
            pass
            self.body._fetch_instances()
        elif _on == ProlinkDjl.PacketType.hello:
            pass
            self.body._fetch_instances()
        elif _on == ProlinkDjl.PacketType.keep_alive:
            pass
            self.body._fetch_instances()
        elif _on == ProlinkDjl.PacketType.number_conflict:
            pass
            self.body._fetch_instances()
        elif _on == ProlinkDjl.PacketType.number_in_use:
            pass
            self.body._fetch_instances()
        else:
            pass
            self.body._fetch_instances()
        _ = self.device_name_raw
        if hasattr(self, '_m_device_name_raw'):
            pass


    class ClaimIpBody(KaitaiStruct):
        """Stage 2, 0x32 bytes. Publishes the IP and proposes a device number.
        `research/02` calls this IdUseRequest.
        """
        def __init__(self, _io, _parent=None, _root=None):
            super(ProlinkDjl.ClaimIpBody, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self._read()

        def _read(self):
            self.ip = self._io.read_bytes(4)
            self.mac = self._io.read_bytes(6)
            self.device_number = self._io.read_u1()
            self.iteration = self._io.read_u1()
            self.role = self._io.read_u1()
            self.assignment_mode = KaitaiStream.resolve_enum(ProlinkDjl.AssignmentMode, self._io.read_u1())


        def _fetch_instances(self):
            pass


    class ClaimMacBody(KaitaiStruct):
        """Stage 1 of the claim chain, 0x2c bytes. Publishes the MAC."""
        def __init__(self, _io, _parent=None, _root=None):
            super(ProlinkDjl.ClaimMacBody, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self._read()

        def _read(self):
            self.iteration = self._io.read_u1()
            self.flags = self._io.read_u1()
            self.mac = self._io.read_bytes(6)


        def _fetch_instances(self):
            pass


    class HelloBody(KaitaiStruct):
        """0x25 bytes total. "I am here", the first thing a device broadcasts."""
        def __init__(self, _io, _parent=None, _root=None):
            super(ProlinkDjl.HelloBody, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self._read()

        def _read(self):
            self.payload = self._io.read_u1()


        def _fetch_instances(self):
            pass


    class KeepAliveBody(KaitaiStruct):
        """Steady state, 0x36 bytes, broadcast every **2.0026 s** -- a tight
        hardware timer, not the 1.5 s `research/02` gives, which traces back to
        what reference *tools* chose (C12). The 10 s device timeout is therefore
        five missed keep-alives, not six or seven.
        """
        def __init__(self, _io, _parent=None, _root=None):
            super(ProlinkDjl.KeepAliveBody, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self._read()

        def _read(self):
            self.device_number = self._io.read_u1()
            self.was_first_on_network = self._io.read_u1()
            self.mac = self._io.read_bytes(6)
            self.ip = self._io.read_bytes(4)
            self.peer_count = self._io.read_u1()
            self.pad_31 = self._io.read_bytes(3)
            self.flags = self._io.read_u1()
            self.trailing = self._io.read_u1()


        def _fetch_instances(self):
            pass


    class NumberBody(KaitaiStruct):
        """Stage 3 (`claim_number`) and `number_in_use`, both 0x26 bytes and
        identical but for the type byte.
        
        `number_in_use` is the surprise. `research/02` §1.7 files type 0x05 under
        mixer channel assignment. What we saw instead: in the same instant a
        joining deck sent its stage-3 claim, an **auto-numbered** deck *unicast*
        one of these back carrying its own number (F36). Reading it as "this
        number is taken" fits what an auto-assigning device must publish, though
        that is inference from a single occurrence.
        """
        def __init__(self, _io, _parent=None, _root=None):
            super(ProlinkDjl.NumberBody, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self._read()

        def _read(self):
            self.device_number = self._io.read_u1()
            self.iteration = self._io.read_u1()


        def _fetch_instances(self):
            pass


    class NumberConflictBody(KaitaiStruct):
        """0x29 bytes, **unicast** by the device that already holds the number.
        Sent in reply to someone else's claim.
        
        Note that silence is not evidence a number is free: XDJ-XZ and Opus Quad
        do not defend their numbers with these at all, so only having watched the
        network is.
        """
        def __init__(self, _io, _parent=None, _root=None):
            super(ProlinkDjl.NumberConflictBody, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self._read()

        def _read(self):
            self.device_number = self._io.read_u1()
            self.ip = self._io.read_bytes(4)


        def _fetch_instances(self):
            pass


    class UnknownBody(KaitaiStruct):
        def __init__(self, _io, _parent=None, _root=None):
            super(ProlinkDjl.UnknownBody, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self._read()

        def _read(self):
            self.rest = self._io.read_bytes_full()


        def _fetch_instances(self):
            pass


    @property
    def device_name_raw(self):
        """The literal 20 bytes, alongside the decoded string. Needed because the
        padding is part of what makes an announcement indistinguishable from a
        real one, and `strz` discards it.
        """
        if hasattr(self, '_m_device_name_raw'):
            return self._m_device_name_raw

        _pos = self._io.pos()
        self._io.seek(12)
        self._m_device_name_raw = self._io.read_bytes(20)
        self._io.seek(_pos)
        return getattr(self, '_m_device_name_raw', None)


