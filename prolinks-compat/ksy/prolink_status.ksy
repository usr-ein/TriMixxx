meta:
  id: prolink_status
  title: Pro DJ Link status, media and settings exchange (UDP 50002)
  license: GPL-2.0-or-later
  endian: be
  encoding: ASCII

doc: |
  The unicast half of Pro DJ Link. Players publish what they are doing and what
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

doc-ref: docs/PROTOCOL.md

seq:
  - id: magic
    contents: "Qspt1WmJOL"

  - id: packet_type
    type: u1
    enum: packet_type
    doc: Byte 0x0a, the discriminator, on the same byte as on port 50000.

  - id: device_name
    size: 20
    type: strz
    doc: |
      0x0b-0x1e. Twenty bytes, not the twenty-one `research/03` §0 states: byte
      0x1f was a constant 0x01 in all 1503 captured packets, which is the same
      shape as the keep-alive where the name runs 0x0c-0x1f and the constant
      sits at 0x20 (C14).

  - id: const_one
    contents: [0x01]
    doc: Byte 0x1f.

instances:
  device_name_raw:
    pos: 0x0b
    size: 20
    doc: The literal bytes, padding included, for byte-diffing against hardware.

  subtype:
    pos: 0x20
    type: u1

  sender_device:
    pos: 0x21
    type: u1
    doc: |
      Who sent this. Present on every type here, which is what lets a receiver
      attribute a packet without looking at the source address.

  body_length:
    pos: 0x22
    type: u2
    doc: Bytes following 0x24.

  # -- media query (0x05) --------------------------------------------------
  #
  # "Device `target_device`, describe slot `slot`." The requester names itself
  # by IP as well as by number, and the reply goes to that address.

  query_requester_ip:
    pos: 0x24
    size: 4

  query_target_device:
    pos: 0x28
    type: u4

  query_slot:
    pos: 0x2c
    type: u4
    enum: media_slot

  # -- media response (0x06) -----------------------------------------------

  response_device:
    pos: 0x24
    type: u4

  response_slot:
    pos: 0x28
    type: u4
    enum: media_slot

  response_volume_name:
    pos: 0x2c
    size: 0x40
    doc: |
      The volume label the DJ formatted the medium with, UTF-16 **big**-endian
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

  response_track_count:
    pos: 0xa4
    type: u4

  response_playlist_count:
    pos: 0xac
    type: u4

  # -- CDJ status (0x0a) ---------------------------------------------------

  status_source_player:
    pos: 0x28
    type: u1
    doc: Which player the loaded track came from; 0 when nothing is loaded.

  status_source_slot:
    pos: 0x29
    type: u1
    enum: media_slot

  status_track_type:
    pos: 0x2a
    type: u1

  status_track_id:
    pos: 0x2c
    type: u4

  status_usb_state:
    pos: 0x6f
    type: u1
    enum: media_state

  status_sd_state:
    pos: 0x73
    type: u1
    enum: media_state

  status_link_available:
    pos: 0x75
    type: u1
    doc: Set when any media is available anywhere on the network.

  status_play_state:
    pos: 0x7b
    type: u1

  status_firmware:
    pos: 0x7c
    size: 4
    type: strz

  status_master_meaningful:
    pos: 0x9e
    type: u1
    doc: |
      Non-zero on whichever player currently holds tempo master: 1 on a
      rekordbox track, 2 on a track with no usable tempo. This is the only place
      mastership is published, so a device that never announces can never know
      who the master is.

  status_bpm_100:
    pos: 0x92
    type: u2
    doc: Tempo in centi-BPM, before the pitch fader is applied.

  status_packet_counter:
    pos: 0xc8
    type: u4

  # -- settings query (0x35) and response (0x36) ---------------------------
  #
  # Adopting a peer's "MY SETTINGS" turns out not to touch the medium's
  # filesystem at all: the requesting deck mounts the NFS export, reads nothing,
  # and asks here instead; the owner reads its own local file and hands the
  # bytes back inline (F38).

  settings_requester:
    pos: 0x24
    type: u1
    doc: |
      The device that wants the settings. In a query this equals
      `sender_device`; in a response it does not, which is how the two are told
      apart without looking at the type byte.

  settings_slot:
    pos: 0x25
    type: u1
    enum: media_slot

  settings_magic:
    pos: 0x28
    type: u4
    doc: |
      Response only. Constant `0x12345678` in the one exchange captured -- the
      same value that leads the payload of `PIONEER/MYSETTING.DAT`, which is
      what ties the file to the wire.

  settings_payload:
    pos: 0x30
    size: 32
    doc: |
      Response only. Deliberately not interpreted: the observed bytes look like
      0x80-based enumerations but nothing maps them to the named options on the
      deck's screen, and a server only has to hand over what the medium holds.

enums:
  packet_type:
    0x05: media_query
    0x06: media_response
    0x0a: cdj_status
    0x29: mixer_status
    0x35: settings_query
    0x36: settings_response

  media_slot:
    0: none
    1: cd
    2: sd
    3: usb
    4: rekordbox

  media_state:
    0x00: loaded
    0x02: unmounting
    0x03: unmounting_alt
    0x04: empty
