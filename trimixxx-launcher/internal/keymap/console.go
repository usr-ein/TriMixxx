package keymap

import (
	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/midimap"
	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/uinput"
)

// Console is the deck as a keyboard for the debug screen.
//
// The debug screen itself does not exist yet -- `trimixxx-debug` is a stub. This
// map is the door left open for it, and it is also the constraint that shapes
// it: the deck has no letter keys and no way to grow any, so whatever ends up
// on that screen has to be drivable with a cursor, a confirm, a cancel and
// seven function keys. In other words: a MENU, not a shell prompt. Anything
// that needs typing needs a USB keyboard plugged in, which is precisely the
// situation the debug screen exists to avoid.
//
// Provisional until there is something to drive; the notes below are the
// reasoning, not a contract.
func Console() *Map {
	m := &Map{
		Name: "console",
		Desc: "the deck as a keyboard for the debug screen (provisional)",
		Notes: map[byte]Binding{
			midimap.NotePlay: {"confirm", []uint16{uinput.KeyEnter}, colLamp},
			midimap.NoteCue:  {"cancel / back", []uint16{uinput.KeyEsc}, colLamp},

			midimap.NoteLoopIn:  {"previous field", []uint16{uinput.KeyLeft}, colLamp},
			midimap.NoteLoopOut: {"next field", []uint16{uinput.KeyRight}, colLamp},
			midimap.NoteReloop:  {"next widget", []uint16{uinput.KeyTab}, RGB{}}, // no LED on the board

			midimap.NoteEncSw: {"confirm", []uint16{uinput.KeyEnter}, RGB{}},
		},

		// A pointer is no use on a text console; the wheel is a fast scroll.
		// The jog is high-resolution (optical, edge-counted), so one key per
		// tick would fly past everything -- hence the divisor.
		Jog:            JogKeys,
		JogCW:          Binding{"scroll down", []uint16{uinput.KeyDown}, RGB{}},
		JogCCW:         Binding{"scroll up", []uint16{uinput.KeyUp}, RGB{}},
		JogTicksPerKey: 8,

		// The track encoder is the deck's list-scrolling control. One detent,
		// one line -- the same feel as picking a track.
		EncoderUp:   Binding{"up one line", []uint16{uinput.KeyUp}, RGB{}},
		EncoderDown: Binding{"down one line", []uint16{uinput.KeyDown}, RGB{}},
	}

	// Ring A: F1..F7, for whatever the screen's top-level actions turn out to
	// be. Function keys because they mean nothing by default and so can be
	// claimed without colliding with anything. Lit dim amber -- one colour,
	// because they are one row of menu slots and nothing distinguishes them
	// until there is a menu to label them.
	for i, key := range []uint16{
		uinput.KeyF1, uinput.KeyF2, uinput.KeyF3, uinput.KeyF4,
		uinput.KeyF5, uinput.KeyF6, uinput.KeyF7,
	} {
		if i >= midimap.PopulatedA {
			break
		}
		m.Notes[byte(midimap.PadABase+i)] = Binding{uinput.KeyName(key), []uint16{key}, RGB{80, 50, 0}}
	}

	// Ring B: plain cursor keys, same positions and the same colours as in the
	// Doom map, so muscle memory carries across.
	for i, b := range []Binding{
		{"escape", []uint16{uinput.KeyEsc}, colMenu},
		{"up", []uint16{uinput.KeyUp}, colUp},
		{"down", []uint16{uinput.KeyDown}, colDown0},
		{"left", []uint16{uinput.KeyLeft}, colLeft},
		{"right", []uint16{uinput.KeyRight}, colRight},
		{"enter", []uint16{uinput.KeyEnter}, colConfirm},
	} {
		if i >= midimap.PopulatedB {
			break
		}
		m.Notes[byte(midimap.PadBBase+i)] = b
	}
	return m
}
