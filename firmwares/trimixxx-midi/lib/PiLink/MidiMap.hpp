#pragma once
#include <stdint.h>

// ===========================================================================
//  MidiMap  -  single source of truth for every TriMixxx MIDI address.
//  Firmware sends/receives on these; the Mixxx mapping matches them exactly.
//  One deck in v1 -> MIDI channel 1 (0-based 0). No address overlaps.
// ===========================================================================
namespace midimap {
    constexpr uint8_t CHANNEL = 0;             // MIDI channel 1

    // ---- Ring pads: node i  <->  note PAD_BASE + i (0..49) ----
    constexpr uint8_t PAD_BASE = 0;            // notes 0x00..0x31
    constexpr uint8_t PAD_COUNT = 50;

    // ---- Transport (play/cue board) ----  notes, LED feedback on same note
    constexpr uint8_t NOTE_PLAY = 60;          // 0x3C
    constexpr uint8_t NOTE_CUE  = 61;          // 0x3D

    // ---- Loop board ----
    constexpr uint8_t NOTE_LOOP_IN  = 62;      // 0x3E
    constexpr uint8_t NOTE_LOOP_OUT = 63;      // 0x3F
    constexpr uint8_t NOTE_RELOOP   = 64;      // 0x40

    // ---- Rotary encoder (track select) ----
    constexpr uint8_t NOTE_ENC_SW = 65;        // 0x41  press = load track
    constexpr uint8_t CC_ENCODER  = 16;        // 0x10  relative: 1=up, 127=down

    // ---- Jog wheel ----
    constexpr uint8_t NOTE_JOG_TOUCH = 66;     // 0x42  touch on/off (scratch)
    constexpr uint8_t CC_JOG         = 17;     // 0x11  relative ticks

    // ---- Tempo fader ----
    constexpr uint8_t CC_TEMPO = 18;           // 0x12  absolute 0..127
}