package main

import (
	"testing"

	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/deck"
	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/keymap"
	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/midimap"
	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/uinput"
)

// recorder stands in for the kernel.
type recorder struct {
	keys []keyEvent
	dx   int32
}

type keyEvent struct {
	code uint16
	down bool
}

func (r *recorder) Key(code uint16, down bool) error {
	r.keys = append(r.keys, keyEvent{code, down})
	return nil
}
func (r *recorder) Move(dx, dy int32) error { r.dx += dx; return nil }
func (r *recorder) Close() error            { return nil }

func (r *recorder) down(code uint16) bool {
	held := false
	for _, e := range r.keys {
		if e.code == code {
			held = e.down
		}
	}
	return held
}

func newTestTranslator(km *keymap.Map) (*translator, *recorder) {
	rec := &recorder{}
	tr := &translator{
		km:        km,
		typist:    &typist{kb: rec, mouse: rec, held: map[uint16]int{}},
		jogScale:  6,
		faderDead: 0.15,
		faderRun:  0.60,
		tempoMSB:  midimap.TempoCenter >> 7,
		tempoLSB:  midimap.TempoCenter & 0x7F,
	}
	return tr, rec
}

// note is what the deck sends for a button, both edges.
func press(tr *translator, n byte)   { tr.handle(deck.Event{Type: deck.NoteOn, Data1: n, Data2: 127}) }
func release(tr *translator, n byte) { tr.handle(deck.Event{Type: deck.NoteOff, Data1: n}) }

// setFader drives the 14-bit pair the way the firmware does: MSB then LSB.
func setFader(tr *translator, v int) {
	tr.handle(deck.Event{Type: deck.ControlChange, Data1: midimap.CCTempo, Data2: byte(v >> 7)})
	tr.handle(deck.Event{Type: deck.ControlChange, Data1: midimap.CCTempoLSB, Data2: byte(v & 0x7F)})
}

// Fire is the jog wheel's touch sensor: hand on the platter, gun goes off. It
// has to hold and release like any other button, which it can because the
// firmware sends a proper Note-On/Note-Off pair for the touch.
func TestJogTouchFires(t *testing.T) {
	tr, rec := newTestTranslator(keymap.Doom())

	press(tr, midimap.NoteJogTouch)
	if !rec.down(uinput.KeyLeftCtrl) {
		t.Error("touching the jog wheel should fire")
	}
	release(tr, midimap.NoteJogTouch)
	if rec.down(uinput.KeyLeftCtrl) {
		t.Error("letting go of the jog wheel should stop firing")
	}
}

func TestButtonsHoldAndRelease(t *testing.T) {
	tr, rec := newTestTranslator(keymap.Doom())

	press(tr, midimap.NoteCue)
	if !rec.down(uinput.KeySpace) {
		t.Error("holding cue should hold use")
	}
	release(tr, midimap.NoteCue)
	if rec.down(uinput.KeySpace) {
		t.Error("releasing cue should release use")
	}
}

// The one that would be a genuine disaster in-game: reloop and the tempo fader
// both mean "run", i.e. both hold LEFTSHIFT. Whichever is released first must
// not take the other's key with it.
func TestSharedKeyIsRefCounted(t *testing.T) {
	tr, rec := newTestTranslator(keymap.Doom())

	press(tr, midimap.NoteReloop)          // run, via the button
	setFader(tr, midimap.TempoCenter+7000) // run, via the fader (far end)
	if !rec.down(uinput.KeyLeftShift) {
		t.Fatal("shift should be held")
	}

	setFader(tr, midimap.TempoCenter) // fader back to neutral
	if !rec.down(uinput.KeyLeftShift) {
		t.Error("the fader released shift while reloop was still held")
	}
	release(tr, midimap.NoteReloop)
	if rec.down(uinput.KeyLeftShift) {
		t.Error("shift stayed down after both holders let go")
	}
}

// The fader as a throttle: stand still near the centre (the pot's centre
// wanders, so this is what stops the player drifting into a wall), walk past the
// deadzone, run at the far end.
func TestFaderThrottle(t *testing.T) {
	tr, rec := newTestTranslator(keymap.Doom())

	setFader(tr, midimap.TempoCenter+100) // a few counts off centre: noise
	if rec.down(uinput.KeyUp) || rec.down(uinput.KeyDown) {
		t.Error("fader noise around the centre detent moved the player")
	}

	setFader(tr, midimap.TempoCenter+4000) // clearly up
	if !rec.down(uinput.KeyUp) {
		t.Error("fader up should walk forward")
	}
	if rec.down(uinput.KeyLeftShift) {
		t.Error("a gentle push should walk, not run")
	}

	setFader(tr, 16383) // all the way up
	if !rec.down(uinput.KeyUp) || !rec.down(uinput.KeyLeftShift) {
		t.Error("fader at the top should run forward")
	}

	setFader(tr, 0) // all the way down
	if rec.down(uinput.KeyUp) {
		t.Error("forward should have been released when the fader crossed centre")
	}
	if !rec.down(uinput.KeyDown) {
		t.Error("fader at the bottom should walk backward")
	}

	setFader(tr, midimap.TempoCenter)
	if rec.down(uinput.KeyUp) || rec.down(uinput.KeyDown) || rec.down(uinput.KeyLeftShift) {
		t.Error("back at centre, the player should be standing still")
	}
}

func TestFaderInvert(t *testing.T) {
	tr, rec := newTestTranslator(keymap.Doom())
	tr.faderInvert = true

	setFader(tr, 16383)
	if !rec.down(uinput.KeyDown) || rec.down(uinput.KeyUp) {
		t.Error("--fader-invert should swap which end walks forward")
	}
}

// The jog wheel is the turn axis, and its sub-pixel carry is what makes a slow
// nudge turn at all instead of being rounded to zero.
func TestJogTurnsAndCarries(t *testing.T) {
	tr, rec := newTestTranslator(keymap.Doom())
	tr.jogScale = 0.4 // deliberately below one pixel per tick

	for range 5 {
		tr.handle(deck.Event{Type: deck.ControlChange, Data1: midimap.CCJog, Data2: 1})
	}
	if rec.dx != 2 { // 5 * 0.4 = 2.0
		t.Errorf("five slow ticks moved %d pixels, want 2", rec.dx)
	}

	// Backwards, using the 7-bit two's complement the firmware sends.
	rec.dx = 0
	tr.jogScale = 6
	tr.jogRemain = 0
	tr.handle(deck.Event{Type: deck.ControlChange, Data1: midimap.CCJog, Data2: 127}) // -1
	if rec.dx != -6 {
		t.Errorf("a backward tick moved %d, want -6", rec.dx)
	}
}

// In console mode the wheel taps keys instead, divided down -- an optical wheel
// at one key per tick would scroll a menu into orbit.
func TestJogKeysAreDividedDown(t *testing.T) {
	km := keymap.Console()
	tr, rec := newTestTranslator(km)

	for range km.JogTicksPerKey - 1 {
		tr.handle(deck.Event{Type: deck.ControlChange, Data1: midimap.CCJog, Data2: 1})
	}
	if len(rec.keys) != 0 {
		t.Fatalf("%d ticks already produced keys, divisor is %d", km.JogTicksPerKey-1, km.JogTicksPerKey)
	}
	tr.handle(deck.Event{Type: deck.ControlChange, Data1: midimap.CCJog, Data2: 1})
	if len(rec.keys) != 2 { // one down, one up
		t.Fatalf("crossing the divisor produced %d key events, want a single tap", len(rec.keys))
	}
	if rec.keys[0].code != uinput.KeyDown {
		t.Errorf("jog forward tapped %s, want down", uinput.KeyName(rec.keys[0].code))
	}
}

// The track encoder scrolls menus, the same job it has in Mixxx -- and it must
// scroll the way the knob turns. The firmware documents CC value 1 as "up" and
// 127 as "down" (MidiMap.hpp), so those are the only two directions anyone here
// is entitled to an opinion about; getting them the wrong way round is
// instantly obvious on the deck and was wrong once already.
func TestEncoderScrollsMenusTheRightWay(t *testing.T) {
	tr, rec := newTestTranslator(keymap.Doom())

	tr.handle(deck.Event{Type: deck.ControlChange, Data1: midimap.CCEncoder, Data2: 1}) // firmware: up
	if len(rec.keys) != 2 || rec.keys[0].code != uinput.KeyUp {
		t.Fatalf("the firmware's 'up' detent sent %v, want an up tap", rec.keys)
	}

	rec.keys = nil
	tr.handle(deck.Event{Type: deck.ControlChange, Data1: midimap.CCEncoder, Data2: 127}) // firmware: down
	if len(rec.keys) != 2 || rec.keys[0].code != uinput.KeyDown {
		t.Fatalf("the firmware's 'down' detent sent %v, want a down tap", rec.keys)
	}
}

// Weapon cycling moved off the encoder and onto PLAY, which firing moved off.
// It has to be Doom's own next-weapon (`]`), not a digit: digits select a
// specific weapon and do nothing at all when you are not carrying it.
func TestPlayCyclesWeapons(t *testing.T) {
	tr, rec := newTestTranslator(keymap.Doom())

	press(tr, midimap.NotePlay)
	if !rec.down(uinput.KeyRightBrace) {
		t.Fatalf("play sent %v, want ] (next weapon)", rec.keys)
	}
	release(tr, midimap.NotePlay)
}

// Left must be left. The wheel's quadrature counts the opposite way round from
// mouse X on this deck, and getting it backwards is instantly obvious in-game
// and easy to reintroduce.
func TestJogDirection(t *testing.T) {
	tr, rec := newTestTranslator(keymap.Doom())
	tr.jogInvert = true

	tr.handle(deck.Event{Type: deck.ControlChange, Data1: midimap.CCJog, Data2: 1}) // one tick forward
	if rec.dx >= 0 {
		t.Errorf("a forward tick moved the pointer %+d; inverted it must go negative", rec.dx)
	}

	rec.dx, tr.jogRemain, tr.jogInvert = 0, 0, false
	tr.handle(deck.Event{Type: deck.ControlChange, Data1: midimap.CCJog, Data2: 1})
	if rec.dx <= 0 {
		t.Errorf("with -jog-invert=false the same tick must go positive, got %+d", rec.dx)
	}
}

// Nothing may be left held when the process goes away: there is no keyboard on
// the deck to un-stick a key with.
func TestCloseReleasesEverything(t *testing.T) {
	tr, rec := newTestTranslator(keymap.Doom())
	press(tr, midimap.NotePlay)
	press(tr, midimap.NoteCue)
	setFader(tr, 16383)

	tr.typist.close()
	for _, code := range []uint16{uinput.KeyLeftCtrl, uinput.KeySpace, uinput.KeyUp, uinput.KeyLeftShift} {
		if rec.down(code) {
			t.Errorf("%s was still held after close", uinput.KeyName(code))
		}
	}
}

// An unmapped control must do nothing at all, rather than something surprising.
func TestUnmappedControlsAreIgnored(t *testing.T) {
	tr, rec := newTestTranslator(keymap.Doom())
	press(tr, byte(midimap.PadABase+midimap.PopulatedA)) // a ring A node that is not fitted
	tr.handle(deck.Event{Type: deck.ControlChange, Data1: 99, Data2: 42})
	if len(rec.keys) != 0 || rec.dx != 0 {
		t.Errorf("unmapped controls produced %v / %d px", rec.keys, rec.dx)
	}
}
