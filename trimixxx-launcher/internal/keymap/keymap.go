// Package keymap says what each deck control does when the deck is pretending
// to be a keyboard.
//
// A Map is pure data: which note holds which key, what the wheels do, how the
// fader is sliced up. `trimixxx-deckkeys` executes it, `--print-map` prints it,
// and the tests check it against the firmware's address table -- so a mapping
// mistake is a failing test rather than a button that quietly does nothing.
package keymap

import (
	"fmt"
	"sort"
	"strings"

	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/midimap"
	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/uinput"
)

// RGB is what a control's LED shows while this mapping is loaded. The zero
// value means "leave it dark", which is how a control says it does nothing here.
//
// Only the ring pads can show a colour (two WS2812s each, driven by SysEx). The
// play, cue and loop LEDs are single fixed-colour lamps the firmware only
// switches on and off, so for those any non-zero colour just means "lit" -- and
// RELOOP has a button but no LED at all on the board, so it can never light up.
type RGB struct{ R, G, B uint8 }

// Lit reports whether this colour asks for anything to be shown.
func (c RGB) Lit() bool { return c != RGB{} }

func (c RGB) String() string { return fmt.Sprintf("#%02X%02X%02X", c.R, c.G, c.B) }

// Binding is one thing a control does: the keys it produces, a label for humans
// reading a log or the control chart, and the colour that says so on the deck.
type Binding struct {
	Label string
	Keys  []uint16
	Color RGB
}

// Bound reports whether this binding does anything at all.
func (b Binding) Bound() bool { return len(b.Keys) > 0 }

func (b Binding) String() string {
	if !b.Bound() {
		return "-"
	}
	names := make([]string, len(b.Keys))
	for i, k := range b.Keys {
		names[i] = uinput.KeyName(k)
	}
	return strings.Join(names, "+")
}

// JogMode is what the jog wheel drives.
type JogMode uint8

const (
	// JogMouse moves the virtual pointer on X. In Doom that is *turning*, and
	// it is the whole reason this bridge synthesises a mouse as well as a
	// keyboard: no key can express "turn 7 degrees".
	JogMouse JogMode = iota
	// JogKeys taps a key every JogTicksPerKey ticks -- for a text UI, where a
	// pointer would be useless but a fast scroll is not.
	JogKeys
)

// Map is a complete deck -> input-device translation.
type Map struct {
	Name string
	Desc string

	// Notes holds a key down for as long as the deck button is held. Ring pads
	// are in here too, by absolute note number.
	Notes map[byte]Binding

	Jog            JogMode
	JogCW, JogCCW  Binding // JogKeys only
	JogTicksPerKey int     // JogKeys only; the wheel is high-resolution

	// One key tap per detent of the track encoder, named the way the FIRMWARE
	// names the two directions ("CC_ENCODER: 1 = up, 127 = down" in
	// MidiMap.hpp) rather than clockwise/anticlockwise. Which way round the
	// knob turns for "up" is a property of how the KY-040 is wired, and is not
	// knowable from here -- so borrowing the one label that is already
	// authoritative is the only way to get this right without a deck in hand.
	// It was wrong the first time precisely because it was called CW/CCW.
	EncoderUp, EncoderDown Binding

	// The tempo fader as a throttle: past the deadzone it holds Forward or
	// Back, and past the run point it adds Run. Leave Forward unbound to ignore
	// the fader entirely.
	FaderForward, FaderBack, FaderRun Binding
}

// UsesFader reports whether the tempo fader is mapped at all.
func (m *Map) UsesFader() bool { return m.FaderForward.Bound() || m.FaderBack.Bound() }

// UsesMouse reports whether this map needs a virtual pointer.
func (m *Map) UsesMouse() bool { return m.Jog == JogMouse }

// Keys is every key code this map can ever emit. A uinput device silently drops
// codes it did not declare at creation time, so this must be exhaustive --
// including the digits EncoderWeapon produces without any Binding to point at.
func (m *Map) Keys() []uint16 {
	seen := map[uint16]bool{}
	add := func(b Binding) {
		for _, k := range b.Keys {
			seen[k] = true
		}
	}
	for _, b := range m.Notes {
		add(b)
	}
	add(m.JogCW)
	add(m.JogCCW)
	add(m.EncoderUp)
	add(m.EncoderDown)
	add(m.FaderForward)
	add(m.FaderBack)
	add(m.FaderRun)
	out := make([]uint16, 0, len(seen))
	for k := range seen {
		out = append(out, k)
	}
	sort.Slice(out, func(i, j int) bool { return out[i] < out[j] })
	return out
}

// For returns a map by name.
func For(name string) (*Map, error) {
	switch name {
	case "doom":
		return Doom(), nil
	case "console":
		return Console(), nil
	}
	return nil, fmt.Errorf("unknown keymap %q (have: doom, console)", name)
}

// Names lists the available maps, for flag help.
func Names() []string { return []string{"doom", "console"} }

// Chart renders the map as the control table a human needs when the thing is
// running on a deck with no screen to explain itself.
func (m *Map) Chart() string {
	var b strings.Builder
	fmt.Fprintf(&b, "%s -- %s\n\n", m.Name, m.Desc)
	fmt.Fprintf(&b, "  %-22s %-14s %-9s %s\n", "DECK CONTROL", "SENDS", "LIGHT", "DOES")
	row := func(control, keys string, c RGB, does string) {
		light := "-"
		if c.Lit() {
			light = c.String()
		}
		fmt.Fprintf(&b, "  %-22s %-14s %-9s %s\n", control, keys, light, does)
	}

	notes := make([]int, 0, len(m.Notes))
	for n := range m.Notes {
		notes = append(notes, int(n))
	}
	sort.Ints(notes)
	for _, n := range notes {
		bind := m.Notes[byte(n)]
		label := bind.Label
		if bind.Color.Lit() && !midimap.HasRGB(byte(n)) {
			// A single-colour lamp: it can only be lit, not coloured.
			label += " (lamp)"
		}
		row(midimap.NoteName(byte(n)), bind.String(), bind.Color, label)
	}

	switch m.Jog {
	case JogMouse:
		row("jog wheel", "mouse X", RGB{}, "turn")
	case JogKeys:
		row("jog wheel CW", m.JogCW.String(), RGB{}, m.JogCW.Label)
		row("jog wheel CCW", m.JogCCW.String(), RGB{}, m.JogCCW.Label)
	}
	if m.EncoderUp.Bound() || m.EncoderDown.Bound() {
		row("track encoder up", m.EncoderUp.String(), RGB{}, m.EncoderUp.Label)
		row("track encoder down", m.EncoderDown.String(), RGB{}, m.EncoderDown.Label)
	}
	if m.UsesFader() {
		row("tempo fader up", m.FaderForward.String(), RGB{}, m.FaderForward.Label)
		row("tempo fader down", m.FaderBack.String(), RGB{}, m.FaderBack.Label)
		if m.FaderRun.Bound() {
			row("tempo fader (far)", m.FaderRun.String(), RGB{}, m.FaderRun.Label)
		}
	}
	return b.String()
}
