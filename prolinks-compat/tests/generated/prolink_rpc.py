# This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild
# type: ignore

import kaitaistruct
from kaitaistruct import KaitaiStruct, KaitaiStream, BytesIO
from enum import IntEnum


if getattr(kaitaistruct, 'API_VERSION', (0, 9)) < (0, 11):
    raise Exception("Incompatible Kaitai Struct Python API: 0.11 or later is required, but you have %s" % (kaitaistruct.__version__))

class ProlinkRpc(KaitaiStruct):
    """The **call** direction of ONC RPC v2 (RFC 1057), plus the argument bodies of
    the eleven procedures a CDJ actually invokes across the three programs it
    expects a peer to run: the portmapper, MOUNT and NFS v2.
    
    This is the serve side. Mixxx already speaks the client half by hand
    (`src/network/prolink/rpc/`), and that half only ever *builds* calls and
    *parses* replies; this schema is what lets it parse the calls a real player
    makes to us. The reply direction stays hand-written -- Kaitai generates no C++
    serializers -- and its unit tests round-trip through the client parsers, so
    both halves of every procedure are checked against each other.
    
    **Only calls.** `msg_type` and `rpc_version` are validated rather than
    switched on, so a reply or a v1/v3 call fails to parse instead of decoding
    into something plausible. On our ports that traffic belongs to somebody else,
    and dropping it is the correct answer.
    
    Three details of Pioneer's usage that this schema pins down:
    
    * **Path and file names are UTF-16LE**, not the ASCII that standard NFS and
      MOUNT use, still length-prefixed, and the prefix counts **bytes** -- so an
      n-character ASCII name announces 2n. This is the single most important
      non-standard fact about the file-access path, and it is why libnfs cannot
      simply be linked: its wire encoder emits ASCII.
    * **Credentials are not enforced.** A player exports to the whole link-local
      subnet. They are parsed here for the record and ignored by the server; being
      stricter than the hardware we are impersonating would only make us the
      reason a real deck fails.
    * **Offsets and sizes are 32-bit**, so NFSv2 cannot address past 4 GiB. Fine
      for audio, but the ceiling must be asserted rather than silently wrapped.
    
    .. seealso::
       docs/PROTOCOL.md
    """

    class AuthFlavor(IntEnum):
        auth_null = 0
        auth_unix = 1
        auth_short = 2

    class IpProtocol(IntEnum):
        tcp = 6
        udp = 17

    class MountProc(IntEnum):
        null_proc = 0
        mnt = 1
        dump = 2
        umnt = 3
        umnt_all = 4
        export = 5

    class NfsProc(IntEnum):
        null_proc = 0
        getattr = 1
        setattr = 2
        lookup = 4
        readlink = 5
        read = 6
        write = 8
        create = 9
        remove = 10
        rename = 11
        link = 12
        symlink = 13
        mkdir = 14
        rmdir = 15
        readdir = 16
        statfs = 17

    class PortmapProc(IntEnum):
        null_proc = 0
        set = 1
        unset = 2
        getport = 3
        dump = 4

    class Program(IntEnum):
        portmap = 100000
        nfs = 100003
        mount = 100005
    def __init__(self, _io, _parent=None, _root=None):
        super(ProlinkRpc, self).__init__(_io)
        self._parent = _parent
        self._root = _root or self
        self._read()

    def _read(self):
        self.xid = self._io.read_u4be()
        self.msg_type = self._io.read_u4be()
        if not self.msg_type == 0:
            raise kaitaistruct.ValidationNotEqualError(0, self.msg_type, self._io, u"/seq/1")
        self.rpc_version = self._io.read_u4be()
        if not self.rpc_version == 2:
            raise kaitaistruct.ValidationNotEqualError(2, self.rpc_version, self._io, u"/seq/2")
        self.program = KaitaiStream.resolve_enum(ProlinkRpc.Program, self._io.read_u4be())
        self.program_version = self._io.read_u4be()
        self.procedure = self._io.read_u4be()
        self.credential = ProlinkRpc.OpaqueAuth(self._io, self, self._root)
        self.verifier = ProlinkRpc.OpaqueAuth(self._io, self, self._root)
        _on = self.call_key
        if _on == 100000003:
            pass
            self.arguments = ProlinkRpc.GetportArgs(self._io, self, self._root)
        elif _on == 100003001:
            pass
            self.arguments = ProlinkRpc.FhandleArgs(self._io, self, self._root)
        elif _on == 100003004:
            pass
            self.arguments = ProlinkRpc.LookupArgs(self._io, self, self._root)
        elif _on == 100003006:
            pass
            self.arguments = ProlinkRpc.ReadArgs(self._io, self, self._root)
        elif _on == 100003016:
            pass
            self.arguments = ProlinkRpc.ReaddirArgs(self._io, self, self._root)
        elif _on == 100003017:
            pass
            self.arguments = ProlinkRpc.FhandleArgs(self._io, self, self._root)
        elif _on == 100005001:
            pass
            self.arguments = ProlinkRpc.PathArgs(self._io, self, self._root)
        elif _on == 100005003:
            pass
            self.arguments = ProlinkRpc.PathArgs(self._io, self, self._root)
        else:
            pass
            self.arguments = ProlinkRpc.VoidArgs(self._io, self, self._root)


    def _fetch_instances(self):
        pass
        self.credential._fetch_instances()
        self.verifier._fetch_instances()
        _on = self.call_key
        if _on == 100000003:
            pass
            self.arguments._fetch_instances()
        elif _on == 100003001:
            pass
            self.arguments._fetch_instances()
        elif _on == 100003004:
            pass
            self.arguments._fetch_instances()
        elif _on == 100003006:
            pass
            self.arguments._fetch_instances()
        elif _on == 100003016:
            pass
            self.arguments._fetch_instances()
        elif _on == 100003017:
            pass
            self.arguments._fetch_instances()
        elif _on == 100005001:
            pass
            self.arguments._fetch_instances()
        elif _on == 100005003:
            pass
            self.arguments._fetch_instances()
        else:
            pass
            self.arguments._fetch_instances()

    class FhandleArgs(KaitaiStruct):
        """NFS `getattr` and `statfs`."""
        def __init__(self, _io, _parent=None, _root=None):
            super(ProlinkRpc.FhandleArgs, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self._read()

        def _read(self):
            self.fhandle = self._io.read_bytes(32)


        def _fetch_instances(self):
            pass


    class GetportArgs(KaitaiStruct):
        """"Which port serves this program?" The gate on everything: a deck asks the
        portmapper for mountd and nfsd *before* it opens dbserver, retries once a
        second indefinitely if nothing answers, and never falls back to the
        well-known ports even when those are bound and idle (F46).
        """
        def __init__(self, _io, _parent=None, _root=None):
            super(ProlinkRpc.GetportArgs, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self._read()

        def _read(self):
            self.program = KaitaiStream.resolve_enum(ProlinkRpc.Program, self._io.read_u4be())
            self.program_version = self._io.read_u4be()
            self.protocol = KaitaiStream.resolve_enum(ProlinkRpc.IpProtocol, self._io.read_u4be())
            self.port = self._io.read_u4be()


        def _fetch_instances(self):
            pass


    class LookupArgs(KaitaiStruct):
        """Walk one path component. A player resolves a track's path one `lookup` per
        directory from the mount root, so this is by far the most frequent call.
        """
        def __init__(self, _io, _parent=None, _root=None):
            super(ProlinkRpc.LookupArgs, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self._read()

        def _read(self):
            self.dir_fhandle = self._io.read_bytes(32)
            self.name = ProlinkRpc.XdrString(self._io, self, self._root)


        def _fetch_instances(self):
            pass
            self.name._fetch_instances()


    class OpaqueAuth(KaitaiStruct):
        """RFC 1057 §7.2: a flavour and a length-prefixed body, padded to four bytes.
        Real players send AUTH_UNIX with a **fresh stamp on every call** -- it is a
        nonce, not the magic constant that documentation and one reference client
        both took it for (C8).
        """
        def __init__(self, _io, _parent=None, _root=None):
            super(ProlinkRpc.OpaqueAuth, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self._read()

        def _read(self):
            self.flavor = KaitaiStream.resolve_enum(ProlinkRpc.AuthFlavor, self._io.read_u4be())
            self.len_body = self._io.read_u4be()
            if not self.len_body <= 400:
                raise kaitaistruct.ValidationGreaterThanError(400, self.len_body, self._io, u"/types/opaque_auth/seq/1")
            self.body = self._io.read_bytes(self.len_body)
            self.padding = self._io.read_bytes((4 - self.len_body % 4) % 4)


        def _fetch_instances(self):
            pass


    class PathArgs(KaitaiStruct):
        """MOUNT `mnt` and `umnt`. The export path, UTF-16LE."""
        def __init__(self, _io, _parent=None, _root=None):
            super(ProlinkRpc.PathArgs, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self._read()

        def _read(self):
            self.path = ProlinkRpc.XdrString(self._io, self, self._root)


        def _fetch_instances(self):
            pass
            self.path._fetch_instances()


    class ReadArgs(KaitaiStruct):
        def __init__(self, _io, _parent=None, _root=None):
            super(ProlinkRpc.ReadArgs, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self._read()

        def _read(self):
            self.fhandle = self._io.read_bytes(32)
            self.offset = self._io.read_u4be()
            self.count = self._io.read_u4be()
            self.total_count = self._io.read_u4be()


        def _fetch_instances(self):
            pass


    class ReaddirArgs(KaitaiStruct):
        def __init__(self, _io, _parent=None, _root=None):
            super(ProlinkRpc.ReaddirArgs, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self._read()

        def _read(self):
            self.fhandle = self._io.read_bytes(32)
            self.cookie = self._io.read_bytes(4)
            self.count = self._io.read_u4be()


        def _fetch_instances(self):
            pass


    class VoidArgs(KaitaiStruct):
        def __init__(self, _io, _parent=None, _root=None):
            super(ProlinkRpc.VoidArgs, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self._read()

        def _read(self):
            self.rest = self._io.read_bytes_full()


        def _fetch_instances(self):
            pass


    class XdrString(KaitaiStruct):
        """A length-prefixed, four-byte-padded byte run. Both the ASCII strings of
        standard XDR and Pioneer's UTF-16LE names travel in this shape; which one
        it is depends on the field, so the bytes are handed back undecoded.
        """
        def __init__(self, _io, _parent=None, _root=None):
            super(ProlinkRpc.XdrString, self).__init__(_io)
            self._parent = _parent
            self._root = _root
            self._read()

        def _read(self):
            self.len_value = self._io.read_u4be()
            if not self.len_value <= 1024:
                raise kaitaistruct.ValidationGreaterThanError(1024, self.len_value, self._io, u"/types/xdr_string/seq/0")
            self.value = self._io.read_bytes(self.len_value)
            self.padding = self._io.read_bytes((4 - self.len_value % 4) % 4)


        def _fetch_instances(self):
            pass


    @property
    def call_key(self):
        """Program and procedure flattened into one switch key. The three program
        numbers are five- and six-digit, so multiplying by 1000 cannot collide
        with any procedure number -- NFS has 18 of them and MOUNT 6.
        
        Both operands are reduced first because neither is validated and Kaitai
        evaluates this into a **signed** 32-bit expression. A datagram naming
        program `0xffffffff` would otherwise overflow it, which is undefined
        behaviour rather than the harmless fall-through to `void_args` that it
        looks like. After reduction the maximum is 999,999,999, and for the three
        real programs the reduction is the identity, so the keys below are
        unaffected.
        """
        if hasattr(self, '_m_call_key'):
            return self._m_call_key

        self._m_call_key = (int(self.program) % 1000000) * 1000 + self.procedure % 1000
        return getattr(self, '_m_call_key', None)


