#pragma once
#include <stdint.h>

// ===========================================================================
//  MidiMap  -  single source of truth for every TriMixxx MIDI address.
//  Firmware sends/receives on these; the Mixxx mapping matches them exactly.
//  One deck in v1 -> MIDI channel 1 (0-based 0). No address overlaps.
// ===========================================================================
namespace midimap {
constexpr uint8_t CHANNEL = 0; // MIDI channel 1

// ---- Ring pads: node i  <->  note PAD_BASE + i (0..49) ----
constexpr uint8_t PAD_BASE  = 0; // notes 0x00..0x31
constexpr uint8_t PAD_COUNT = 50;

// ---- Transport (play/cue board) ----  notes, LED feedback on same note
constexpr uint8_t NOTE_PLAY = 60; // 0x3C
constexpr uint8_t NOTE_CUE  = 61; // 0x3D

// ---- Loop board ----
constexpr uint8_t NOTE_LOOP_IN  = 62; // 0x3E
constexpr uint8_t NOTE_LOOP_OUT = 63; // 0x3F
constexpr uint8_t NOTE_RELOOP   = 64; // 0x40

// ---- Rotary encoder (track select) ----
constexpr uint8_t NOTE_ENC_SW = 65; // 0x41  press = load track
constexpr uint8_t CC_ENCODER  = 16; // 0x10  relative: 1=up, 127=down

// ---- Jog wheel ----
constexpr uint8_t NOTE_JOG_TOUCH = 66; // 0x42  touch on/off (scratch)
constexpr uint8_t CC_JOG         = 17; // 0x11  relative ticks

// ---- Tempo fader (14-bit high-res: MSB + LSB pair) ----
// Standard MIDI 14-bit convention: LSB rides on CC = MSB + 32. Mixxx binds both
// with <fourteen-bit-msb>/<fourteen-bit-lsb>. Combined 0..16383, 8192 = center.
constexpr uint8_t CC_TEMPO     = 18; // 0x12  MSB (high 7 bits)
constexpr uint8_t CC_TEMPO_LSB = 50; // 0x32  LSB (low 7 bits) = 18 + 32

// ===========================================================================
//  SysEx (Mixxx -> firmware).  Layout:  F0 MFR_ID CMD <args...> F7
//
//  Every byte between the markers must be 7-bit (0x00..0x7F) -- that is a hard
//  SysEx rule, not a convention. ttymidi carries the payload opaquely (see its
//  README, "System Exclusive"), so this layout is purely between Mixxx and us.
// ===========================================================================
constexpr uint8_t SYSEX_MFR_ID = 0x7D; // reserved non-commercial/educational ID

// ---- CMD 0x01: set a ring node's RGB LEDs ----
//   F0 7D 01 <node> <12 nibbles> F7   -> LED0 and LED1 set independently
//   F0 7D 01 <node> < 6 nibbles> F7   -> one colour, mirrored onto BOTH LEDs
//
// Each 8-bit colour channel travels as TWO data bytes, high nibble first:
//   value 0..255  ->  [v >> 4, v & 0x0F]   (both <= 0x0F, so 7-bit safe)
// This is what buys the full 0..255 per channel: a single 7-bit byte would cap
// each channel at 127 (half brightness). Channel order is R,G,B -- same as the
// OneButton wire slot (R0 G0 B0 R1 G1 B1), so no reordering anywhere.
constexpr uint8_t SYSEX_CMD_RING_LED      = 0x01;
constexpr uint8_t SYSEX_RING_LED_ARGS_ONE = 1 + 6;  // node + 3 channels x 2 nibbles
constexpr uint8_t SYSEX_RING_LED_ARGS_TWO = 1 + 12; // node + 6 channels x 2 nibbles

// ---- CMD 0x02: reboot the S3 (equivalent to the physical RESET button) ----
//   F0 7D 02 52 53 54 F7      ("RST")
// The magic is required so a malformed/stray SysEx can never reboot the deck
// mid-set; a bare `F0 7D 02 F7` is ignored.
constexpr uint8_t SYSEX_CMD_RESET      = 0x02;
constexpr uint8_t SYSEX_RESET_MAGIC[3] = {0x52, 0x53, 0x54}; // 'R','S','T'
} // namespace midimap