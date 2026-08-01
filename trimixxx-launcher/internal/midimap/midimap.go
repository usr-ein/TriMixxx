// Package midimap mirrors the deck firmware's MIDI address table.
//
// The authority is ../../../firmwares/trimixxx-midi/lib/PiLink/MidiMap.hpp. This
// file is a transcription of it, and the two must be changed together -- exactly
// as mixxx_config/TriMixxx.midi.xml must. Nothing here may drift: a wrong
// constant means a button silently does the wrong thing, with no error anywhere.
package midimap

import "fmt"

// Channel is the deck's MIDI channel, 0-based (= channel 1). One deck, one
// channel; everything below is on it.
const Channel = 0

// Ring pads. Node i of a ring is note <base>+i. The 50-wide reservations match
// the firmware's compile-time cap (RING_A_NODES / RING_B_NODES in main.cpp) --
// the rings self-size at boot to however many boards are actually populated, so
// the counts below are the ceiling, not the population.
const (
	PadABase  = 0 // notes 0x00..0x31
	PadACount = 50
	PadBBase  = 67 // notes 0x43..0x74
	PadBCount = 50
)

// PopulatedA / PopulatedB are how many nodes are physically on each ring today
// (7 and 6). Only used to decide what to *map*; the firmware would happily
// address all 50. Growing a ring never renumbers anything, so raising these is
// the only change a new board needs on this side.
const (
	PopulatedA = 7
	PopulatedB = 6
)

// Named controls. LED feedback rides back on the same note: Note-On with
// velocity > 0 lights it, Note-Off (or velocity 0) clears it.
const (
	NotePlay     = 60 // 0x3C
	NoteCue      = 61 // 0x3D
	NoteLoopIn   = 62 // 0x3E
	NoteLoopOut  = 63 // 0x3F
	NoteReloop   = 64 // 0x40  (button only, no LED)
	NoteEncSw    = 65 // 0x41  track encoder press
	NoteJogTouch = 66 // 0x42  jog touch on/off

	CCEncoder  = 16 // 0x10  relative: 1 = one detent up, 127 = one down
	CCJog      = 17 // 0x11  relative 7-bit two's-complement ticks
	CCTempo    = 18 // 0x12  14-bit MSB...
	CCTempoLSB = 50 // 0x32  ...and its LSB (= MSB + 32, the standard convention)
)

// TempoCenter is the 14-bit value the fader's centre detent produces, i.e. the
// midpoint of 0..16383. The firmware derives the value ratiometrically, so this
// needs no calibration.
const TempoCenter = 8192

// SysEx to the firmware -- included for completeness and because the ring-LED
// command is the only way to set a pad's actual colour (a Note-On velocity can
// only set white brightness). Layout: F0 7D <cmd> <args...> F7, every payload
// byte 7-bit.
const (
	SysExMfrID    = 0x7D
	SysExCmdRingA = 0x01
	SysExCmdReset = 0x02
	SysExCmdRingB = 0x03
)

// NoteName renders a note number as the control it belongs to, for logs and for
// the printed control chart. Ring pads are named by ring and node, which is how
// they are labelled everywhere else in the project.
func NoteName(note byte) string {
	switch note {
	case NotePlay:
		return "play"
	case NoteCue:
		return "cue"
	case NoteLoopIn:
		return "loop in"
	case NoteLoopOut:
		return "loop out"
	case NoteReloop:
		return "reloop"
	case NoteEncSw:
		return "encoder press"
	case NoteJogTouch:
		return "jog touch"
	}
	if note >= PadABase && note < PadABase+PadACount {
		return fmt.Sprintf("ring A pad %d", int(note)-PadABase)
	}
	if note >= PadBBase && note < PadBBase+PadBCount {
		return fmt.Sprintf("ring B pad %d", int(note)-PadBBase)
	}
	return fmt.Sprintf("note %d", note)
}

// RingOf reports which ring a note belongs to: the SysEx command that sets that
// ring's LEDs, and the node index within it. Not a ring pad -> ok is false.
func RingOf(note byte) (cmd byte, node byte, ok bool) {
	if note >= PadABase && note < PadABase+PadACount {
		return SysExCmdRingA, note - PadABase, true
	}
	if note >= PadBBase && note < PadBBase+PadBCount {
		return SysExCmdRingB, note - PadBBase, true
	}
	return 0, 0, false
}

// HasRGB reports whether this control's LED can show a colour. Only ring pads
// can (two WS2812s each); play/cue/loop are single fixed-colour lamps.
func HasRGB(note byte) bool {
	_, _, ok := RingOf(note)
	return ok
}

// HasLamp reports whether this control has an on/off LED behind it.
//
// RELOOP is the odd one out and the reason this exists: it has a button and a
// note, but the loop board has no LED for it (see LoopBoard's own comment), so
// nothing can ever light it up. Anything painting the deck needs to know that
// rather than wondering why one button stays dark.
func HasLamp(note byte) bool {
	switch note {
	case NotePlay, NoteCue, NoteLoopIn, NoteLoopOut:
		return true
	}
	return false
}

// JogDelta decodes a CC_JOG value: 7-bit two's complement, so 1..63 is forward
// and 65..127 (i.e. -63..-1) is backward. The firmware splits a large movement
// into several such chunks rather than wrapping.
func JogDelta(v byte) int {
	if v > 63 {
		return int(v) - 128
	}
	return int(v)
}

// EncoderDelta decodes a CC_ENCODER value. The track encoder is a detent
// encoder: the firmware sends one message per detent, 1 up / 127 down.
func EncoderDelta(v byte) int {
	switch v {
	case 1:
		return +1
	case 127:
		return -1
	}
	return 0
}
