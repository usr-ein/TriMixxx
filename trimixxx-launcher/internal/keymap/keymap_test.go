package keymap

import (
	"strings"
	"testing"

	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/midimap"
	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/uinput"
)

// Every note a map binds must be a note the deck can actually send. A typo here
// produces a pad that does nothing, with no error anywhere -- so the addresses
// are checked against the firmware's table instead.
func TestBoundNotesExistOnTheDeck(t *testing.T) {
	valid := map[byte]bool{
		midimap.NotePlay: true, midimap.NoteCue: true,
		midimap.NoteLoopIn: true, midimap.NoteLoopOut: true, midimap.NoteReloop: true,
		midimap.NoteEncSw: true, midimap.NoteJogTouch: true,
	}
	for i := range midimap.PopulatedA {
		valid[byte(midimap.PadABase+i)] = true
	}
	for i := range midimap.PopulatedB {
		valid[byte(midimap.PadBBase+i)] = true
	}

	for _, name := range Names() {
		m, err := For(name)
		if err != nil {
			t.Fatalf("For(%q): %v", name, err)
		}
		for note, b := range m.Notes {
			if !valid[note] {
				t.Errorf("%s: note %d (%s) is not a populated deck control, bound to %q",
					name, note, midimap.NoteName(note), b.Label)
			}
			if !b.Bound() {
				t.Errorf("%s: %s is bound to nothing", name, midimap.NoteName(note))
			}
			if b.Label == "" {
				t.Errorf("%s: %s has no label", name, midimap.NoteName(note))
			}
		}
	}
}

// Keys() feeds the uinput device declaration. Anything missing from it is
// silently dropped by the kernel at runtime -- which looks exactly like a
// broken mapping -- so it must cover every binding in the map.
func TestKeysCoversEverythingEmittable(t *testing.T) {
	for _, name := range Names() {
		m, _ := For(name)
		declared := map[uint16]bool{}
		for _, k := range m.Keys() {
			declared[k] = true
		}
		check := func(b Binding, what string) {
			for _, k := range b.Keys {
				if !declared[k] {
					t.Errorf("%s: %s emits %s, which Keys() does not declare", name, what, uinput.KeyName(k))
				}
			}
		}
		for note, b := range m.Notes {
			check(b, midimap.NoteName(note))
		}
		check(m.JogCW, "jog cw")
		check(m.JogCCW, "jog ccw")
		check(m.EncoderUp, "encoder up")
		check(m.EncoderDown, "encoder down")
		check(m.FaderForward, "fader forward")
		check(m.FaderBack, "fader back")
		check(m.FaderRun, "fader run")
	}
}

// Anything with a colour must be something that can actually show one. The trap
// is RELOOP: it has a button and a note but no LED on the loop board, so
// colouring it would be a promise the hardware cannot keep.
func TestOnlyLightableControlsAreLit(t *testing.T) {
	for _, name := range Names() {
		m, _ := For(name)
		for note, b := range m.Notes {
			if !b.Color.Lit() {
				continue
			}
			if !midimap.HasRGB(note) && !midimap.HasLamp(note) {
				t.Errorf("%s: %s is given colour %s but has no LED behind it",
					name, midimap.NoteName(note), b.Color)
			}
		}
	}
}

// Within a ring, no two Doom pads may share a colour: one colour per function
// is the entire point of lighting them, and this is what stops a palette edit
// from quietly making two weapons the same shade.
//
// Only the Doom map. The console map paints its whole F-key row one colour on
// purpose -- those seven pads have no distinct functions to distinguish until
// there is a menu behind them.
func TestDoomRingColoursAreDistinct(t *testing.T) {
	m := Doom()
	seen := map[byte]map[RGB]byte{} // ring cmd -> colour -> first note using it
	for note, b := range m.Notes {
		cmd, _, ok := midimap.RingOf(note)
		if !ok || !b.Color.Lit() {
			continue
		}
		if seen[cmd] == nil {
			seen[cmd] = map[RGB]byte{}
		}
		if prev, clash := seen[cmd][b.Color]; clash {
			t.Errorf("%s and %s are both %s",
				midimap.NoteName(prev), midimap.NoteName(note), b.Color)
		}
		seen[cmd][b.Color] = note
	}
}

// Every populated pad on both rings must be lit in Doom: an unlit pad reads as
// "this one does nothing", and in this mapping they all do something.
func TestEveryDoomPadIsLit(t *testing.T) {
	m := Doom()
	for i := range midimap.PopulatedA {
		if b := m.Notes[byte(midimap.PadABase+i)]; !b.Color.Lit() {
			t.Errorf("ring A pad %d (%s) is dark", i, b.Label)
		}
	}
	for i := range midimap.PopulatedB {
		if b := m.Notes[byte(midimap.PadBBase+i)]; !b.Color.Lit() {
			t.Errorf("ring B pad %d (%s) is dark", i, b.Label)
		}
	}
}

// The Doom map is what makes the feature work at all; these are the specific
// promises the doom/ README and Doom's own defaults are written against.
func TestDoomMap(t *testing.T) {
	m := Doom()

	// Fire is the jog wheel's touch sensor, not a button.
	if got := m.Notes[midimap.NoteJogTouch].Keys; len(got) != 1 || got[0] != uinput.KeyLeftCtrl {
		t.Errorf("touching the jog wheel should fire (ctrl), got %v", m.Notes[midimap.NoteJogTouch])
	}
	if !m.UsesMouse() {
		t.Error("the jog wheel must drive the mouse; it is the turn axis")
	}
	if !m.UsesFader() {
		t.Error("the tempo fader must be the throttle")
	}

	// You have to be able to quit from the deck alone: Esc, a cursor, Enter,
	// and a literal 'y' for the confirmation prompt.
	need := map[uint16]bool{
		uinput.KeyEsc: false, uinput.KeyEnter: false, uinput.KeyY: false,
		uinput.KeyUp: false, uinput.KeyDown: false,
	}
	for _, b := range m.Notes {
		for _, k := range b.Keys {
			if _, ok := need[k]; ok {
				need[k] = true
			}
		}
	}
	for k, found := range need {
		if !found {
			t.Errorf("no deck button sends %s -- Doom could be entered but not left", uinput.KeyName(k))
		}
	}

	// All seven weapons, one per populated ring A node.
	for i, key := range uinput.Weapons {
		b, ok := m.Notes[byte(midimap.PadABase+i)]
		if !ok {
			t.Fatalf("ring A pad %d unbound; expected weapon %d", i, i+1)
		}
		if len(b.Keys) != 1 || b.Keys[0] != key {
			t.Errorf("ring A pad %d sends %v, want weapon key %s", i, b, uinput.KeyName(key))
		}
	}
}

func TestUnknownMap(t *testing.T) {
	if _, err := For("quake"); err == nil {
		t.Fatal("For(\"quake\") should fail")
	}
}

// The chart is what a human reads when the deck is in front of them and the
// source is not, so make sure it actually renders the controls.
func TestChart(t *testing.T) {
	got := Doom().Chart()
	for _, want := range []string{"play", "ctrl", "fire", "jog wheel", "mouse X", "tempo fader"} {
		if !strings.Contains(got, want) {
			t.Errorf("chart is missing %q:\n%s", want, got)
		}
	}
}
