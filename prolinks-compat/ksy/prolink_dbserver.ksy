meta:
  id: prolink_dbserver
  title: Pro DJ Link dbserver message (TCP 1051)
  license: GPL-2.0-or-later
  endian: be

doc: |
  One message of the "remotedb" protocol -- the one the LINK button drives, and
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

seq:
  - id: magic_tag
    type: u1
    valid: 0x11
    doc: The magic is itself a tagged UInt32, tag included.
  - id: magic
    type: u4
    valid: 0x872349ae
  - id: transaction_id_tag
    type: u1
    valid: 0x11
  - id: transaction_id
    type: u4
    doc: |
      Echoed in the reply, and the only way to pair one with its request. A
      player uses 0xfffffffe for Introduce and Disconnect and counts up from
      about 0x03800001 for everything else.
  - id: message_type_tag
    type: u1
    valid: 0x10
  - id: message_type
    type: u2
    doc: |
      Requests are 0x0nnn-0x3nnn, replies 0x4nnn. 0x2003 GetArtwork is answered
      by 0x4002 Artwork; 0x4003 is a refusal.
  - id: num_args_tag
    type: u1
    valid: 0x0f
  - id: num_args
    type: u1
    valid:
      max: 12
  - id: arg_tags_tag
    type: u1
    valid: 0x14
  - id: len_arg_tags
    type: u4
    valid: 12
  - id: arg_tags
    size: len_arg_tags
    doc: |
      The *other* numbering. Twelve bytes, one per possible argument, describing
      the same arguments the tag bytes in the stream describe -- but with
      different values for the same five types (02/03/06 here against
      0f/10/11/14/26 there). Both must agree or the message is rejected, so a
      writer has to fill in two unrelated tables consistently.
  - id: args
    type: 'argument(arg_tags[_index], _index == 0 ? 1 : args[_index - 1].num_value)'
    repeat: expr
    repeat-expr: num_args

types:
  argument:
    params:
      - id: arg_tag
        type: u1
        doc: This argument's entry in the header's 12-byte tag blob.
      - id: prev_value
        type: u4
        doc: |
          The preceding argument's numeric value, or 1 for the first argument and
          for any predecessor that was not a number. Only used to decide whether
          this argument is on the wire at all -- see is_present.
    seq:
      - id: field
        type: field
        if: is_present
    instances:
      is_present:
        value: 'not (arg_tag == 0x03 and prev_value == 0)'
        doc: |
          **A zero-length binary argument is omitted from the wire entirely.**
          Not sent as an empty blob: simply absent, with the preceding UInt32
          length argument the only thing that says so. It is the rule that
          desynchronises a naive parser -- a reader that expects the blob
          consumes the next message's magic as a field, and every argument after
          that is one position out with no error to show for it.

          A player answers GetArtwork for a track with no art exactly this way,
          so it is the common case rather than an exotic one.
      num_value:
        value: 'is_present ? field.num_value : 1'
        doc: For the next argument's is_present. 1 means "not a zero length".

  field:
    doc: One tagged value. The tag byte is the first numbering (see arg_tags).
    seq:
      - id: field_type
        type: u1
        valid:
          any-of: [0x0f, 0x10, 0x11, 0x14, 0x26]
      - id: value_u8
        type: u1
        if: field_type == 0x0f
      - id: value_u16
        type: u2
        if: field_type == 0x10
      - id: value_u32
        type: u4
        if: field_type == 0x11
      - id: len_blob
        type: u4
        if: field_type == 0x14
        valid:
          max: 16777216
        doc: |
          Capped, because the runtime allocates the buffer before it discovers
          the stream is shorter than the length claims: without this one corrupt
          word asks for four gigabytes. Well above any real payload -- the
          largest thing this protocol carries is a cover image.
      - id: blob
        size: len_blob
        if: field_type == 0x14
      - id: num_chars
        type: u4
        if: field_type == 0x26
        valid:
          max: 8388608
        doc: Capped for the same reason as len_blob, and doubled below.
      - id: text_raw
        size: num_chars * 2
        if: field_type == 0x26
        doc: |
          UTF-16 **big**-endian, and the prefix counts *characters* including the
          terminating NUL -- so a three-character string announces 4 and carries
          8 bytes.

          Left as raw bytes rather than given an `encoding:`, deliberately:
          Mixxx compiles the Kaitai runtime with KS_STR_ENCODING_NONE, under
          which bytes_to_str returns its input unchanged. An encoding would
          therefore be silently ignored and the caller would get UTF-16 bytes in
          a std::string it believed was decoded. The caller converts.

          Note also that this is the opposite convention to the NFS half of the
          same protocol, which sends UTF-16 *little*-endian counted in *bytes*.
          The two must never share a helper.
    instances:
      num_value:
        value: >-
          field_type == 0x0f ? value_u8 :
          field_type == 0x10 ? value_u16 :
          field_type == 0x11 ? value_u32 : 1
        doc: |
          The integer this field carries, or 1 if it is not an integer -- which
          is what argument::is_present needs, since only a genuine zero length
          means the next binary argument was omitted.
