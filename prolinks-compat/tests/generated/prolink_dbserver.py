# This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild
# type: ignore

import kaitaistruct
from kaitaistruct import KaitaiStruct, KaitaiStream, BytesIO


if getattr(kaitaistruct, 'API_VERSION', (0, 9)) < (0, 11):
    raise Exception("Incompatible Kaitai Struct Python API: 0.11 or later is required, but you have %s" % (kaitaistruct.__version__))

class ProlinkDbserver(KaitaiStruct):
    """One message of the "remotedb" protocol -- the one the LINK button drives, and
    the only way to get album art out of a player: a real CDJ never asks NFS for an
    image (docs/FINDINGS.md F49).
    
    Written from docs/PROTOCOL.md section 5. The format round-trips byte-exactly
    against 1957 messages captured from two CDJ-2000NXS (F7).
    
    **This schema parses exactly one message**, not a stream of them. Messages
    carry no length prefix and are framed by nothing but their own contents, so the
    only way to know whether a TCP buffer holds a whole one is to try: running off
    the end is the *expected* outcome of trying too early, and the caller
    distinguishes that (an EOF from the runtime) from a structural error (a
    validation failure) to decide between "wait for more" and "drop the
    connection". The parser's final stream position is how many bytes it consumed.
    
    Kaitai cannot generate C++ serializers, so this is the read direction only.
    The writers are hand-written in
    `mixxx/src/network/prolink/dbserver/dbservermessage.cpp` and their unit tests
    compare against vectors produced by the Python proof-of-concept.
    """
    def __init__(self, _io, _parent=None, _root=None):
        super(ProlinkDbserver, self).__init__(_io)
        self._parent = _parent
        self._root = _root or self
        self._read()

    def _read(self):
        self.magic_tag = self._io.read_u1()
        if not self.magic_tag == 17:
            raise kaitaistruct.ValidationNotEqualError(17, self.magic_tag, self._io, u"/seq/0")
        self.magic = self._io.read_u4be()
        if not self.magic == 2267236782:
            raise kaitaistruct.ValidationNotEqualError(2267236782, self.magic, self._io, u"/seq/1")
        self.transaction_id_tag = self._io.read_u1()
        if not self.transaction_id_tag == 17:
            raise kaitaistruct.ValidationNotEqualError(17, self.transaction_id_tag, self._io, u"/seq/2")
        self.transaction_id = self._io.read_u4be()
        self.message_type_tag = self._io.read_u1()
        if not self.message_type_tag == 16:
            raise kaitaistruct.ValidationNotEqualError(16, self.message_type_tag, self._io, u"/seq/4")
        self.message_type = self._io.read_u2be()
        self.num_args_tag = self._io.read_u1()
        if not self.num_args_tag == 15:
            raise kaitaistruct.ValidationNotEqualError(15, self.num_args_tag, self._io, u"/seq/6")
        self.num_args = self._io.read_u1()
        if not self.num_args <= 12:
            raise kaitaistruct.ValidationGreaterThanError(12, self.num_args, self._io, u"/seq/7")
        self.arg_tags_tag = self._io.read_u1()
        if not self.arg_tags_tag == 20:
            raise kaitaistruct.ValidationNotEqualError(20, self.arg_tags_tag, self._io, u"/seq/8")
        self.len_arg_tags = self._io.read_u4be()
        if not self.len_arg_tags == 12:
            raise kaitaistruct.ValidationNotEqualError(12, self.len_arg_tags, self._io, u"/seq/9")
        self.arg_tags = self._io.read_bytes(self.len_arg_tags)
        self.args = []
        for i in range(self.num_args):
            self.args.append(ProlinkDbserver.Argument(KaitaiStream.byte_array_index(self.arg_tags, i), (1 if i == 0 else self.args[i - 1].num_value), self._io, self, self._root))



    def _fetch_instances(self):
        pass
        for i in range(len(self.args)):
            pass
            self.args[i]._fetch_instances()


    class Argument(KaitaiStruct):
        def __init__(self, arg_tag, prev_value, _io, _parent=None, _root=None):
            super(ProlinkDbserver.Argument, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self.arg_tag = arg_tag
            self.prev_value = prev_value
            self._read()

        def _read(self):
            if self.is_present:
                pass
                self.field = ProlinkDbserver.Field(self._io, self, self._root)



        def _fetch_instances(self):
            pass
            if self.is_present:
                pass
                self.field._fetch_instances()


        @property
        def is_present(self):
            """**A zero-length binary argument is omitted from the wire entirely.**
            Not sent as an empty blob: simply absent, with the preceding UInt32
            length argument the only thing that says so. It is the rule that
            desynchronises a naive parser -- a reader that expects the blob
            consumes the next message's magic as a field, and every argument after
            that is one position out with no error to show for it.
            
            A player answers GetArtwork for a track with no art exactly this way,
            so it is the common case rather than an exotic one.
            """
            if hasattr(self, '_m_is_present'):
                return self._m_is_present

            self._m_is_present = (not ( ((self.arg_tag == 3) and (self.prev_value == 0)) ))
            return getattr(self, '_m_is_present', None)

        @property
        def num_value(self):
            """For the next argument's is_present. 1 means "not a zero length"."""
            if hasattr(self, '_m_num_value'):
                return self._m_num_value

            self._m_num_value = (self.field.num_value if self.is_present else 1)
            return getattr(self, '_m_num_value', None)


    class Field(KaitaiStruct):
        """One tagged value. The tag byte is the first numbering (see arg_tags)."""
        def __init__(self, _io, _parent=None, _root=None):
            super(ProlinkDbserver.Field, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self._read()

        def _read(self):
            self.field_type = self._io.read_u1()
            if not  ((self.field_type == 15) or (self.field_type == 16) or (self.field_type == 17) or (self.field_type == 20) or (self.field_type == 38)) :
                raise kaitaistruct.ValidationNotAnyOfError(self.field_type, self._io, u"/types/field/seq/0")
            if self.field_type == 15:
                pass
                self.value_u8 = self._io.read_u1()

            if self.field_type == 16:
                pass
                self.value_u16 = self._io.read_u2be()

            if self.field_type == 17:
                pass
                self.value_u32 = self._io.read_u4be()

            if self.field_type == 20:
                pass
                self.len_blob = self._io.read_u4be()
                if not self.len_blob <= 16777216:
                    raise kaitaistruct.ValidationGreaterThanError(16777216, self.len_blob, self._io, u"/types/field/seq/4")

            if self.field_type == 20:
                pass
                self.blob = self._io.read_bytes(self.len_blob)

            if self.field_type == 38:
                pass
                self.num_chars = self._io.read_u4be()
                if not self.num_chars <= 8388608:
                    raise kaitaistruct.ValidationGreaterThanError(8388608, self.num_chars, self._io, u"/types/field/seq/6")

            if self.field_type == 38:
                pass
                self.text_raw = self._io.read_bytes(self.num_chars * 2)



        def _fetch_instances(self):
            pass
            if self.field_type == 15:
                pass

            if self.field_type == 16:
                pass

            if self.field_type == 17:
                pass

            if self.field_type == 20:
                pass

            if self.field_type == 20:
                pass

            if self.field_type == 38:
                pass

            if self.field_type == 38:
                pass


        @property
        def num_value(self):
            """The integer this field carries, or 1 if it is not an integer -- which
            is what argument::is_present needs, since only a genuine zero length
            means the next binary argument was omitted.
            """
            if hasattr(self, '_m_num_value'):
                return self._m_num_value

            self._m_num_value = (self.value_u8 if self.field_type == 15 else (self.value_u16 if self.field_type == 16 else (self.value_u32 if self.field_type == 17 else 1)))
            return getattr(self, '_m_num_value', None)



