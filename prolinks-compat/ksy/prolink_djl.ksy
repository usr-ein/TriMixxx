meta:
  id: prolink_djl
  title: Pro DJ Link discovery and device numbering (UDP 50000)
  license: GPL-2.0-or-later
  endian: be
  encoding: ASCII

doc: |
  The broadcast announcement protocol Pioneer players and mixers use to find
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

doc-ref: docs/PROTOCOL.md

seq:
  - id: magic
    contents: "Qspt1WmJOL"
    doc: Present on every Pro DJ Link datagram, on all three UDP ports.

  - id: packet_type
    type: u1
    enum: packet_type
    doc: |
      Byte 0x0a, the discriminator. The values are not ordered by handshake
      position.

  - id: subtype
    type: u1
    doc: |
      Byte 0x0b. Zero on everything we have observed; kept distinct from
      `stype` below because the two are different bytes that both look like
      length or variant markers.

  - id: device_name
    size: 20
    type: strz
    doc: |
      NUL-padded. `CDJ-2000nexus` is the exact casing, captured literally
      rather than inferred (F1) -- `research/02` §4.1 guessed at it and a
      mis-cased name is the kind of thing a peer could plausibly reject.

  - id: const_one
    contents: [0x01]
    doc: Byte 0x20. Invariant across every packet in every capture.

  - id: device_kind
    type: u1
    enum: device_kind
    doc: Byte 0x21. Critical for impersonation.

  - id: pad_22
    contents: [0x00]

  - id: stype
    type: u1
    doc: |
      Byte 0x23. **Equals the total datagram length** for every type we have
      seen. `research/02` §0.1 gives claim_number a length of 0x2a against an
      stype of 0x26; six real type-0x04 packets are 0x26 bytes long, so the
      document's length column is simply wrong there (C2).

  - id: body
    type:
      switch-on: packet_type
      cases:
        'packet_type::hello': hello_body
        'packet_type::claim_mac': claim_mac_body
        'packet_type::claim_ip': claim_ip_body
        'packet_type::claim_number': number_body
        'packet_type::number_in_use': number_body
        'packet_type::keep_alive': keep_alive_body
        'packet_type::number_conflict': number_conflict_body
        _: unknown_body
    doc: |
      Everything from 0x24 on. Unknown types decode to `unknown_body` rather
      than failing: a mixer, a CDJ-3000 or a newer firmware may send types we
      have never seen, and a parser that throws would take out discovery
      entirely for the devices we *do* understand.

instances:
  device_name_raw:
    pos: 0xc
    size: 20
    doc: |
      The literal 20 bytes, alongside the decoded string. Needed because the
      padding is part of what makes an announcement indistinguishable from a
      real one, and `strz` discards it.

types:
  hello_body:
    doc: 0x25 bytes total. "I am here", the first thing a device broadcasts.
    seq:
      - id: payload
        type: u1

  claim_mac_body:
    doc: Stage 1 of the claim chain, 0x2c bytes. Publishes the MAC.
    seq:
      - id: iteration
        type: u1
        doc: 1, 2, 3 -- the packet's position in its 3-packet burst.
      - id: flags
        type: u1
      - id: mac
        size: 6

  claim_ip_body:
    doc: |
      Stage 2, 0x32 bytes. Publishes the IP and proposes a device number.
      `research/02` calls this IdUseRequest.
    seq:
      - id: ip
        size: 4
      - id: mac
        size: 6
      - id: device_number
        type: u1
        doc: Byte 0x2e. The number being proposed, not yet held.
      - id: iteration
        type: u1
      - id: role
        type: u1
        doc: |
          Byte 0x30. A CDJ/mixer role, **not** a constant: a DJM-2000nexus
          sends 0x02 where a CDJ sends 0x01 (C1), and `research/02` documents
          it as invariant.
      - id: assignment_mode
        type: u1
        enum: assignment_mode
        doc: |
          Byte 0x31 (F36). Every capture before F36 had both decks numbered
          manually, so only `manual` had ever been seen and `research/02`
          marked this settled on documentation alone.

  number_body:
    doc: |
      Stage 3 (`claim_number`) and `number_in_use`, both 0x26 bytes and
      identical but for the type byte.

      `number_in_use` is the surprise. `research/02` §1.7 files type 0x05 under
      mixer channel assignment. What we saw instead: in the same instant a
      joining deck sent its stage-3 claim, an **auto-numbered** deck *unicast*
      one of these back carrying its own number (F36). Reading it as "this
      number is taken" fits what an auto-assigning device must publish, though
      that is inference from a single occurrence.
    seq:
      - id: device_number
        type: u1
      - id: iteration
        type: u1

  keep_alive_body:
    doc: |
      Steady state, 0x36 bytes, broadcast every **2.0026 s** -- a tight
      hardware timer, not the 1.5 s `research/02` gives, which traces back to
      what reference *tools* chose (C12). The 10 s device timeout is therefore
      five missed keep-alives, not six or seven.
    seq:
      - id: device_number
        type: u1
      - id: was_first_on_network
        type: u1
        doc: |
          Byte 0x25: 0x02 if this device was first onto the network, 0x01 if
          peers were already present. Latched at boot and never re-evaluated --
          a deck held 0x02 while its peer count went 1 to 2 (F9). It is not a
          CDJ/mixer role byte as documented, and not the peer count.
      - id: mac
        size: 6
      - id: ip
        size: 4
      - id: peer_count
        type: u1
        doc: Byte 0x30.
      - id: pad_31
        size: 3
      - id: flags
        type: u1
        doc: Byte 0x34. Tracks the device kind, like `claim_ip`'s role byte.
      - id: trailing
        type: u1
        doc: |
          Byte 0x35. **0x00 on nexus hardware, not 0x01** as `research/02` has
          it (C3). 0x64 is required for CDJ-3000 coexistence.

  number_conflict_body:
    doc: |
      0x29 bytes, **unicast** by the device that already holds the number.
      Sent in reply to someone else's claim.

      Note that silence is not evidence a number is free: XDJ-XZ and Opus Quad
      do not defend their numbers with these at all, so only having watched the
      network is.
    seq:
      - id: device_number
        type: u1
      - id: ip
        size: 4

  unknown_body:
    seq:
      - id: rest
        size-eos: true

enums:
  packet_type:
    0x00: claim_mac
    0x01: mixer_assign_intent
    0x02: claim_ip
    0x03: mixer_assign
    0x04: claim_number
    0x05: number_in_use
    0x06: keep_alive
    0x08: number_conflict
    0x0a: hello

  device_kind:
    0x01: mixer
    0x02: cdj
    0x03: rekordbox_or_cdj3000
    0x04: cdj3000_hello

  assignment_mode:
    0x01: auto
    0x02: manual
