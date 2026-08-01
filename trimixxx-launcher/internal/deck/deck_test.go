package deck

import (
	"os"
	"testing"

	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/midimap"
)

// Decode is the boundary between the firmware's wire format and everything
// above it, so pin down exactly what it accepts.
func TestDecode(t *testing.T) {
	for _, tc := range []struct {
		name string
		in   []byte
		want Event
		ok   bool
	}{
		{"play pressed", []byte{0x90, midimap.NotePlay, 127}, Event{NoteOn, midimap.NotePlay, 127}, true},
		{"play released", []byte{0x80, midimap.NotePlay, 0}, Event{NoteOff, midimap.NotePlay, 0}, true},
		// The standard MIDI idiom: a Note-On with velocity 0 is a release.
		{"note-on velocity 0 is a release", []byte{0x90, midimap.NoteCue, 0}, Event{NoteOff, midimap.NoteCue, 0}, true},
		{"jog tick", []byte{0xB0, midimap.CCJog, 3}, Event{ControlChange, midimap.CCJog, 3}, true},
		{"tempo msb", []byte{0xB0, midimap.CCTempo, 64}, Event{ControlChange, midimap.CCTempo, 64}, true},
		// One deck, one channel: anything else is not this deck talking.
		{"wrong channel", []byte{0x91, midimap.NotePlay, 127}, Event{}, false},
		{"pitch bend", []byte{0xE0, 0, 64}, Event{}, false},
		{"truncated", []byte{0x90, midimap.NotePlay}, Event{}, false},
		{"empty", nil, Event{}, false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			got, ok := Decode(tc.in)
			if ok != tc.ok {
				t.Fatalf("Decode(% X) ok = %v, want %v", tc.in, ok, tc.ok)
			}
			if ok && got != tc.want {
				t.Fatalf("Decode(% X) = %v, want %v", tc.in, got, tc.want)
			}
		})
	}
}

// The jog and encoder deltas are relative encodings that are easy to get subtly
// wrong (and would then only show up as a wheel that turns the wrong way).
func TestRelativeDeltas(t *testing.T) {
	for _, tc := range []struct {
		v    byte
		want int
	}{{0, 0}, {1, 1}, {63, 63}, {127, -1}, {65, -63}} {
		if got := midimap.JogDelta(tc.v); got != tc.want {
			t.Errorf("JogDelta(%d) = %d, want %d", tc.v, got, tc.want)
		}
	}
	for _, tc := range []struct {
		v    byte
		want int
	}{{1, +1}, {127, -1}, {0, 0}, {64, 0}} {
		if got := midimap.EncoderDelta(tc.v); got != tc.want {
			t.Errorf("EncoderDelta(%d) = %d, want %d", tc.v, got, tc.want)
		}
	}
}

// The ring-LED SysEx. Every payload byte must be 7-bit -- a hard SysEx rule --
// so each 8-bit colour channel travels as two nibbles, high first. Sending a
// raw 0..255 byte would both break the frame and cap every channel at half
// brightness, and the only symptom would be "the colours look wrong".
func TestPadFrame(t *testing.T) {
	got := padFrame(midimap.SysExCmdRingA, 3, 0xFF, 0xBE, 0x00)
	want := []byte{
		0xF0, 0x7D, 0x01, 0x03,
		0x0F, 0x0F, // R = FF
		0x0B, 0x0E, // G = BE
		0x00, 0x00, // B = 00
		0xF7,
	}
	if string(got) != string(want) {
		t.Fatalf("padFrame = % X, want % X", got, want)
	}
	// The firmware counts node + 6 colour bytes = 7 args (SYSEX_RING_LED_ARGS_ONE,
	// "one colour on both LEDs") and ignores anything else outright.
	if n := len(got) - 4; n != 7 {
		t.Errorf("frame carries %d args, firmware accepts 7 or 13", n)
	}
	for i, b := range got[1 : len(got)-1] {
		if b > 0x7F {
			t.Errorf("payload byte %d is %#02x, which is not 7-bit", i, b)
		}
	}
	if cmd, _, _ := midimap.RingOf(midimap.PadBBase); cmd != midimap.SysExCmdRingB {
		t.Error("ring B pads must be addressed with the ring B command")
	}
}

// A missing /proc/asound (any dev machine, or a Linux box with no ALSA) must
// read as "alive", or the link would flap forever off the Pi.
func TestAliveFailsOpen(t *testing.T) {
	if _, err := os.ReadFile(seqClients); err == nil {
		t.Skipf("%s exists here; nothing to prove", seqClients)
	}
	if !alive("definitely-not-a-real-alsa-client") {
		t.Error("alive() reported dead with no ALSA sequencer present")
	}
}
