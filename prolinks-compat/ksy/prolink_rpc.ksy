meta:
  id: prolink_rpc
  title: ONC RPC v2 calls a Pioneer player makes to a file server (UDP)
  license: GPL-2.0-or-later
  endian: be
  encoding: ASCII

doc: |
  The **call** direction of ONC RPC v2 (RFC 1057), plus the argument bodies of
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

doc-ref: docs/PROTOCOL.md

seq:
  - id: xid
    type: u4
    doc: |
      The caller's correlation token. Echoed verbatim in the reply and otherwise
      meaningless to us -- notably it is *not* a sequence number we may validate.

  - id: msg_type
    type: u4
    valid: 0
    doc: 0 is CALL. A reply on our port is not ours to answer.

  - id: rpc_version
    type: u4
    valid: 2

  - id: program
    type: u4
    enum: program

  - id: program_version
    type: u4

  - id: procedure
    type: u4

  - id: credential
    type: opaque_auth

  - id: verifier
    type: opaque_auth
    doc: AUTH_NULL with an empty body on every call we have seen.

  - id: arguments
    type:
      switch-on: call_key
      cases:
        100000003: getport_args
        100005001: path_args
        100005003: path_args
        100003001: fhandle_args
        100003004: lookup_args
        100003006: read_args
        100003016: readdir_args
        100003017: fhandle_args
        _: void_args
    doc: |
      Dispatched on program and procedure together, because the procedure
      numbers collide across programs -- MOUNT's `mnt` and NFS's `getattr` are
      both 1.

      Everything unrecognised parses as `void_args` rather than failing. The
      procedures with no arguments (`null`, portmap `dump`, MOUNT `export`) are
      genuinely empty, and for anything else a server that can at least read the
      header can answer PROC_UNAVAIL -- which is a real answer, and what a
      player expects when it probes for a procedure that is not implemented.

instances:
  call_key:
    value: '(program.to_i % 1000000) * 1000 + (procedure % 1000)'
    doc: |
      Program and procedure flattened into one switch key. The three program
      numbers are five- and six-digit, so multiplying by 1000 cannot collide
      with any procedure number -- NFS has 18 of them and MOUNT 6.

      Both operands are reduced first because neither is validated and Kaitai
      evaluates this into a **signed** 32-bit expression. A datagram naming
      program `0xffffffff` would otherwise overflow it, which is undefined
      behaviour rather than the harmless fall-through to `void_args` that it
      looks like. After reduction the maximum is 999,999,999, and for the three
      real programs the reduction is the identity, so the keys below are
      unaffected.

types:
  opaque_auth:
    doc: |
      RFC 1057 §7.2: a flavour and a length-prefixed body, padded to four bytes.
      Real players send AUTH_UNIX with a **fresh stamp on every call** -- it is a
      nonce, not the magic constant that documentation and one reference client
      both took it for (C8).
    seq:
      - id: flavor
        type: u4
        enum: auth_flavor
      - id: len_body
        type: u4
        valid:
          max: 400
        doc: RFC 1057's own ceiling on an opaque_auth body.
      - id: body
        size: len_body
      - id: padding
        size: '(4 - (len_body % 4)) % 4'

  xdr_string:
    doc: |
      A length-prefixed, four-byte-padded byte run. Both the ASCII strings of
      standard XDR and Pioneer's UTF-16LE names travel in this shape; which one
      it is depends on the field, so the bytes are handed back undecoded.
    seq:
      - id: len_value
        type: u4
        valid:
          max: 1024
        doc: |
          Capped so a corrupt or hostile datagram claiming a 4 GiB name costs a
          parse failure rather than an allocation. The longest real path on a
          rekordbox medium is a few hundred bytes.
      - id: value
        size: len_value
      - id: padding
        size: '(4 - (len_value % 4)) % 4'

  getport_args:
    doc: |
      "Which port serves this program?" The gate on everything: a deck asks the
      portmapper for mountd and nfsd *before* it opens dbserver, retries once a
      second indefinitely if nothing answers, and never falls back to the
      well-known ports even when those are bound and idle (F46).
    seq:
      - id: program
        type: u4
        enum: program
      - id: program_version
        type: u4
      - id: protocol
        type: u4
        enum: ip_protocol
      - id: port
        type: u4
        doc: Ignored in a GETPORT; the reply carries the answer.

  path_args:
    doc: MOUNT `mnt` and `umnt`. The export path, UTF-16LE.
    seq:
      - id: path
        type: xdr_string

  fhandle_args:
    doc: NFS `getattr` and `statfs`.
    seq:
      - id: fhandle
        size: 32

  lookup_args:
    doc: |
      Walk one path component. A player resolves a track's path one `lookup` per
      directory from the mount root, so this is by far the most frequent call.
    seq:
      - id: dir_fhandle
        size: 32
      - id: name
        type: xdr_string
        doc: UTF-16LE. See the top-level doc.

  read_args:
    seq:
      - id: fhandle
        size: 32
      - id: offset
        type: u4
      - id: count
        type: u4
      - id: total_count
        type: u4
        doc: |
          Deprecated already in RFC 1094 and ignored by every server, including
          this one. Parsed so the argument block is fully accounted for.

  readdir_args:
    seq:
      - id: fhandle
        size: 32
      - id: cookie
        size: 4
        doc: |
          Opaque position in the listing; all zeroes means "from the start".
          Opaque to the *client*, that is -- the server mints it, so we are free
          to make it an index.
      - id: count
        type: u4
        doc: Maximum reply size in bytes, not a number of entries.

  void_args:
    seq:
      - id: rest
        size-eos: true

enums:
  program:
    100000: portmap
    100003: nfs
    100005: mount

  auth_flavor:
    0: auth_null
    1: auth_unix
    2: auth_short

  ip_protocol:
    6: tcp
    17: udp

  portmap_proc:
    0: null_proc
    1: set
    2: unset
    3: getport
    4: dump

  mount_proc:
    0: null_proc
    1: mnt
    2: dump
    3: umnt
    4: umnt_all
    5: export

  nfs_proc:
    0: null_proc
    1: getattr
    2: setattr
    4: lookup
    5: readlink
    6: read
    8: write
    9: create
    10: remove
    11: rename
    12: link
    13: symlink
    14: mkdir
    15: rmdir
    16: readdir
    17: statfs
