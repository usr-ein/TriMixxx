# This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild
# type: ignore

import kaitaistruct
from kaitaistruct import KaitaiStruct, KaitaiStream, BytesIO
from enum import IntEnum


if getattr(kaitaistruct, 'API_VERSION', (0, 9)) < (0, 11):
    raise Exception("Incompatible Kaitai Struct Python API: 0.11 or later is required, but you have %s" % (kaitaistruct.__version__))

class ProlinkStatus(KaitaiStruct):
    """The unicast half of Pro DJ Link. Players publish what they are doing and what
    is in their slots here, and ask each other the two questions that a browse
    depends on: "what is in your slot N?" and "give me the settings on it".
    
    **This port is invisible until you announce.** A player unicasts on 50002 to
    peers that have announced themselves and to nobody else -- 1507 status packets
    in one session all went deck-to-deck, and not one reached a host that had been
    on the network the whole time without announcing (F21). Slot occupancy is
    published here and *nowhere else* (F20), which is why the virtual CDJ is a
    hard prerequisite for both browsing a deck and being browsed by one.
    
    **The header is not the one on port 50000.** The device name occupies
    0x0b-0x1e -- one byte earlier and one byte shorter -- and byte 0x1f is a
    structural 0x01 where the discovery header has its name's last byte (C14).
    Reusing `prolink_djl.ksy` here yields plausible nonsense rather than an error,
    so the two schemas are deliberately kept apart.
    
    **Why the type-specific fields are `instances` and not a `body` switch.**
    These packets are sparse: a 284-byte status packet has about a dozen fields we
    can name and 260 bytes we cannot, and of 749 consecutive packets from an idle
    CDJ-2000nexus only six bytes ever changed. Declaring the unknown runs as
    padding would be inventing structure. Instances carry the one thing that is
    actually known -- an absolute offset -- they read exactly like the offset
    tables in `docs/PROTOCOL.md`, and being lazy they cost nothing for the fields a
    given packet type does not have.
    
    .. seealso::
       docs/PROTOCOL.md
    """

    class MediaSlot(IntEnum):
        none = 0
        cd = 1
        sd = 2
        usb = 3
        rekordbox = 4

    class MediaState(IntEnum):
        loaded = 0
        unmounting = 2
        unmounting_alt = 3
        empty = 4

    class PacketType(IntEnum):
        media_query = 5
        media_response = 6
        cdj_status = 10
        mixer_status = 41
        settings_query = 53
        settings_response = 54
    def __init__(self, _io, _parent=None, _root=None):
        super(ProlinkStatus, self).__init__(_io)
        self._parent = _parent
        self._root = _root or self
        self._read()

    def _read(self):
        self.magic = self._io.read_bytes(10)
        if not self.magic == b"\x51\x73\x70\x74\x31\x57\x6D\x4A\x4F\x4C":
            raise kaitaistruct.ValidationNotEqualError(b"\x51\x73\x70\x74\x31\x57\x6D\x4A\x4F\x4C", self.magic, self._io, u"/seq/0")
        self.packet_type = KaitaiStream.resolve_enum(ProlinkStatus.PacketType, self._io.read_u1())
        self.device_name = (KaitaiStream.bytes_terminate(self._io.read_bytes(20), 0, False)).decode(u"ASCII")
        self.const_one = self._io.read_bytes(1)
        if not self.const_one == b"\x01":
            raise kaitaistruct.ValidationNotEqualError(b"\x01", self.const_one, self._io, u"/seq/3")


    def _fetch_instances(self):
        pass
        _ = self.body_length
        if hasattr(self, '_m_body_length'):
            pass

        _ = self.device_name_raw
        if hasattr(self, '_m_device_name_raw'):
            pass

        _ = self.query_requester_ip
        if hasattr(self, '_m_query_requester_ip'):
            pass

        _ = self.query_slot
        if hasattr(self, '_m_query_slot'):
            pass

        _ = self.query_target_device
        if hasattr(self, '_m_query_target_device'):
            pass

        _ = self.response_device
        if hasattr(self, '_m_response_device'):
            pass

        _ = self.response_playlist_count
        if hasattr(self, '_m_response_playlist_count'):
            pass

        _ = self.response_slot
        if hasattr(self, '_m_response_slot'):
            pass

        _ = self.response_track_count
        if hasattr(self, '_m_response_track_count'):
            pass

        _ = self.response_volume_name
        if hasattr(self, '_m_response_volume_name'):
            pass

        _ = self.sender_device
        if hasattr(self, '_m_sender_device'):
            pass

        _ = self.settings_magic
        if hasattr(self, '_m_settings_magic'):
            pass

        _ = self.settings_payload
        if hasattr(self, '_m_settings_payload'):
            pass

        _ = self.settings_requester
        if hasattr(self, '_m_settings_requester'):
            pass

        _ = self.settings_slot
        if hasattr(self, '_m_settings_slot'):
            pass

        _ = self.status_bpm_100
        if hasattr(self, '_m_status_bpm_100'):
            pass

        _ = self.status_firmware
        if hasattr(self, '_m_status_firmware'):
            pass

        _ = self.status_link_available
        if hasattr(self, '_m_status_link_available'):
            pass

        _ = self.status_master_meaningful
        if hasattr(self, '_m_status_master_meaningful'):
            pass

        _ = self.status_packet_counter
        if hasattr(self, '_m_status_packet_counter'):
            pass

        _ = self.status_play_state
        if hasattr(self, '_m_status_play_state'):
            pass

        _ = self.status_sd_state
        if hasattr(self, '_m_status_sd_state'):
            pass

        _ = self.status_source_player
        if hasattr(self, '_m_status_source_player'):
            pass

        _ = self.status_source_slot
        if hasattr(self, '_m_status_source_slot'):
            pass

        _ = self.status_track_id
        if hasattr(self, '_m_status_track_id'):
            pass

        _ = self.status_track_type
        if hasattr(self, '_m_status_track_type'):
            pass

        _ = self.status_usb_state
        if hasattr(self, '_m_status_usb_state'):
            pass

        _ = self.subtype
        if hasattr(self, '_m_subtype'):
            pass


    @property
    def body_length(self):
        """Bytes following 0x24."""
        if hasattr(self, '_m_body_length'):
            return self._m_body_length

        _pos = self._io.pos()
        self._io.seek(34)
        self._m_body_length = self._io.read_u2be()
        self._io.seek(_pos)
        return getattr(self, '_m_body_length', None)

    @property
    def device_name_raw(self):
        """The literal bytes, padding included, for byte-diffing against hardware."""
        if hasattr(self, '_m_device_name_raw'):
            return self._m_device_name_raw

        _pos = self._io.pos()
        self._io.seek(11)
        self._m_device_name_raw = self._io.read_bytes(20)
        self._io.seek(_pos)
        return getattr(self, '_m_device_name_raw', None)

    @property
    def query_requester_ip(self):
        if hasattr(self, '_m_query_requester_ip'):
            return self._m_query_requester_ip

        _pos = self._io.pos()
        self._io.seek(36)
        self._m_query_requester_ip = self._io.read_bytes(4)
        self._io.seek(_pos)
        return getattr(self, '_m_query_requester_ip', None)

    @property
    def query_slot(self):
        if hasattr(self, '_m_query_slot'):
            return self._m_query_slot

        _pos = self._io.pos()
        self._io.seek(44)
        self._m_query_slot = KaitaiStream.resolve_enum(ProlinkStatus.MediaSlot, self._io.read_u4be())
        self._io.seek(_pos)
        return getattr(self, '_m_query_slot', None)

    @property
    def query_target_device(self):
        if hasattr(self, '_m_query_target_device'):
            return self._m_query_target_device

        _pos = self._io.pos()
        self._io.seek(40)
        self._m_query_target_device = self._io.read_u4be()
        self._io.seek(_pos)
        return getattr(self, '_m_query_target_device', None)

    @property
    def response_device(self):
        if hasattr(self, '_m_response_device'):
            return self._m_response_device

        _pos = self._io.pos()
        self._io.seek(36)
        self._m_response_device = self._io.read_u4be()
        self._io.seek(_pos)
        return getattr(self, '_m_response_device', None)

    @property
    def response_playlist_count(self):
        if hasattr(self, '_m_response_playlist_count'):
            return self._m_response_playlist_count

        _pos = self._io.pos()
        self._io.seek(172)
        self._m_response_playlist_count = self._io.read_u4be()
        self._io.seek(_pos)
        return getattr(self, '_m_response_playlist_count', None)

    @property
    def response_slot(self):
        if hasattr(self, '_m_response_slot'):
            return self._m_response_slot

        _pos = self._io.pos()
        self._io.seek(40)
        self._m_response_slot = KaitaiStream.resolve_enum(ProlinkStatus.MediaSlot, self._io.read_u4be())
        self._io.seek(_pos)
        return getattr(self, '_m_response_slot', None)

    @property
    def response_track_count(self):
        if hasattr(self, '_m_response_track_count'):
            return self._m_response_track_count

        _pos = self._io.pos()
        self._io.seek(164)
        self._m_response_track_count = self._io.read_u4be()
        self._io.seek(_pos)
        return getattr(self, '_m_response_track_count', None)

    @property
    def response_volume_name(self):
        """The volume label the DJ formatted the medium with, UTF-16 **big**-endian
        -- like the dbserver strings and unlike the NFS layer's UTF-16LE.
        
        Raw bytes on purpose. Mixxx compiles the Kaitai runtime with
        `KS_STR_ENCODING_NONE`, under which an `encoding: UTF-16BE` is silently a
        no-op: it would hand back these same bytes in a string that claimed to be
        decoded, correct for ASCII and mojibake for everything else. Decode in the
        caller.
        
        Fixed 64 bytes, NUL-padded, and the padding is not a terminator -- the
        field is always this long and the name simply stops. **Often empty and
        legitimately so**: an unlabelled stick reports no name while carrying a
        full library, so emptiness here is not emptiness of the slot.
        """
        if hasattr(self, '_m_response_volume_name'):
            return self._m_response_volume_name

        _pos = self._io.pos()
        self._io.seek(44)
        self._m_response_volume_name = self._io.read_bytes(64)
        self._io.seek(_pos)
        return getattr(self, '_m_response_volume_name', None)

    @property
    def sender_device(self):
        """Who sent this. Present on every type here, which is what lets a receiver
        attribute a packet without looking at the source address.
        """
        if hasattr(self, '_m_sender_device'):
            return self._m_sender_device

        _pos = self._io.pos()
        self._io.seek(33)
        self._m_sender_device = self._io.read_u1()
        self._io.seek(_pos)
        return getattr(self, '_m_sender_device', None)

    @property
    def settings_magic(self):
        """Response only. Constant `0x12345678` in the one exchange captured -- the
        same value that leads the payload of `PIONEER/MYSETTING.DAT`, which is
        what ties the file to the wire.
        """
        if hasattr(self, '_m_settings_magic'):
            return self._m_settings_magic

        _pos = self._io.pos()
        self._io.seek(40)
        self._m_settings_magic = self._io.read_u4be()
        self._io.seek(_pos)
        return getattr(self, '_m_settings_magic', None)

    @property
    def settings_payload(self):
        """Response only. Deliberately not interpreted: the observed bytes look like
        0x80-based enumerations but nothing maps them to the named options on the
        deck's screen, and a server only has to hand over what the medium holds.
        """
        if hasattr(self, '_m_settings_payload'):
            return self._m_settings_payload

        _pos = self._io.pos()
        self._io.seek(48)
        self._m_settings_payload = self._io.read_bytes(32)
        self._io.seek(_pos)
        return getattr(self, '_m_settings_payload', None)

    @property
    def settings_requester(self):
        """The device that wants the settings. In a query this equals
        `sender_device`; in a response it does not, which is how the two are told
        apart without looking at the type byte.
        """
        if hasattr(self, '_m_settings_requester'):
            return self._m_settings_requester

        _pos = self._io.pos()
        self._io.seek(36)
        self._m_settings_requester = self._io.read_u1()
        self._io.seek(_pos)
        return getattr(self, '_m_settings_requester', None)

    @property
    def settings_slot(self):
        if hasattr(self, '_m_settings_slot'):
            return self._m_settings_slot

        _pos = self._io.pos()
        self._io.seek(37)
        self._m_settings_slot = KaitaiStream.resolve_enum(ProlinkStatus.MediaSlot, self._io.read_u1())
        self._io.seek(_pos)
        return getattr(self, '_m_settings_slot', None)

    @property
    def status_bpm_100(self):
        """Tempo in centi-BPM, before the pitch fader is applied."""
        if hasattr(self, '_m_status_bpm_100'):
            return self._m_status_bpm_100

        _pos = self._io.pos()
        self._io.seek(146)
        self._m_status_bpm_100 = self._io.read_u2be()
        self._io.seek(_pos)
        return getattr(self, '_m_status_bpm_100', None)

    @property
    def status_firmware(self):
        if hasattr(self, '_m_status_firmware'):
            return self._m_status_firmware

        _pos = self._io.pos()
        self._io.seek(124)
        self._m_status_firmware = (KaitaiStream.bytes_terminate(self._io.read_bytes(4), 0, False)).decode(u"ASCII")
        self._io.seek(_pos)
        return getattr(self, '_m_status_firmware', None)

    @property
    def status_link_available(self):
        """Set when any media is available anywhere on the network."""
        if hasattr(self, '_m_status_link_available'):
            return self._m_status_link_available

        _pos = self._io.pos()
        self._io.seek(117)
        self._m_status_link_available = self._io.read_u1()
        self._io.seek(_pos)
        return getattr(self, '_m_status_link_available', None)

    @property
    def status_master_meaningful(self):
        """Non-zero on whichever player currently holds tempo master: 1 on a
        rekordbox track, 2 on a track with no usable tempo. This is the only place
        mastership is published, so a device that never announces can never know
        who the master is.
        """
        if hasattr(self, '_m_status_master_meaningful'):
            return self._m_status_master_meaningful

        _pos = self._io.pos()
        self._io.seek(158)
        self._m_status_master_meaningful = self._io.read_u1()
        self._io.seek(_pos)
        return getattr(self, '_m_status_master_meaningful', None)

    @property
    def status_packet_counter(self):
        if hasattr(self, '_m_status_packet_counter'):
            return self._m_status_packet_counter

        _pos = self._io.pos()
        self._io.seek(200)
        self._m_status_packet_counter = self._io.read_u4be()
        self._io.seek(_pos)
        return getattr(self, '_m_status_packet_counter', None)

    @property
    def status_play_state(self):
        if hasattr(self, '_m_status_play_state'):
            return self._m_status_play_state

        _pos = self._io.pos()
        self._io.seek(123)
        self._m_status_play_state = self._io.read_u1()
        self._io.seek(_pos)
        return getattr(self, '_m_status_play_state', None)

    @property
    def status_sd_state(self):
        if hasattr(self, '_m_status_sd_state'):
            return self._m_status_sd_state

        _pos = self._io.pos()
        self._io.seek(115)
        self._m_status_sd_state = KaitaiStream.resolve_enum(ProlinkStatus.MediaState, self._io.read_u1())
        self._io.seek(_pos)
        return getattr(self, '_m_status_sd_state', None)

    @property
    def status_source_player(self):
        """Which player the loaded track came from; 0 when nothing is loaded."""
        if hasattr(self, '_m_status_source_player'):
            return self._m_status_source_player

        _pos = self._io.pos()
        self._io.seek(40)
        self._m_status_source_player = self._io.read_u1()
        self._io.seek(_pos)
        return getattr(self, '_m_status_source_player', None)

    @property
    def status_source_slot(self):
        if hasattr(self, '_m_status_source_slot'):
            return self._m_status_source_slot

        _pos = self._io.pos()
        self._io.seek(41)
        self._m_status_source_slot = KaitaiStream.resolve_enum(ProlinkStatus.MediaSlot, self._io.read_u1())
        self._io.seek(_pos)
        return getattr(self, '_m_status_source_slot', None)

    @property
    def status_track_id(self):
        if hasattr(self, '_m_status_track_id'):
            return self._m_status_track_id

        _pos = self._io.pos()
        self._io.seek(44)
        self._m_status_track_id = self._io.read_u4be()
        self._io.seek(_pos)
        return getattr(self, '_m_status_track_id', None)

    @property
    def status_track_type(self):
        if hasattr(self, '_m_status_track_type'):
            return self._m_status_track_type

        _pos = self._io.pos()
        self._io.seek(42)
        self._m_status_track_type = self._io.read_u1()
        self._io.seek(_pos)
        return getattr(self, '_m_status_track_type', None)

    @property
    def status_usb_state(self):
        if hasattr(self, '_m_status_usb_state'):
            return self._m_status_usb_state

        _pos = self._io.pos()
        self._io.seek(111)
        self._m_status_usb_state = KaitaiStream.resolve_enum(ProlinkStatus.MediaState, self._io.read_u1())
        self._io.seek(_pos)
        return getattr(self, '_m_status_usb_state', None)

    @property
    def subtype(self):
        if hasattr(self, '_m_subtype'):
            return self._m_subtype

        _pos = self._io.pos()
        self._io.seek(32)
        self._m_subtype = self._io.read_u1()
        self._io.seek(_pos)
        return getattr(self, '_m_subtype', None)


