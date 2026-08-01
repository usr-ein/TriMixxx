package keymap

import (
	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/midimap"
	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/uinput"
)

// The deck's Doom palette. Every control that does something is lit, and the
// colour says what KIND of thing it does, so the deck reads as a Doom
// controller at a glance instead of as a DJ deck with the wrong labels.
//
// Ring A is the weapons, ramped by how much they hurt -- white, yellow, orange,
// red -- except the last three, which take the colours the game itself uses:
// violet for rockets, blue for plasma, green for the BFG.
//
// Ring B is the menu, one distinct colour per key so a cursor direction can be
// picked out without counting round the ring.
//
// The two rings repeat a few colours between them (white, red, green). They are
// physically separate rings on opposite sides of the deck, so there is nothing
// to confuse; within a ring every colour is unique.
var (
	colFist    = RGB{255, 255, 255} // white
	colPistol  = RGB{255, 190, 0}   // yellow
	colShotgun = RGB{255, 80, 0}    // orange
	colChain   = RGB{255, 0, 0}     // red
	colRocket  = RGB{160, 0, 255}   // violet
	colPlasma  = RGB{0, 180, 255}   // cyan -- plasma is blue in-game
	colBFG     = RGB{0, 255, 0}     // green -- so is the BFG

	colDown0   = RGB{255, 0, 0}     // red    -- walk BACKWARD
	colUp      = RGB{0, 255, 0}     // green  -- walk FORWARD
	colMenu    = RGB{255, 190, 0}   // yellow -- esc / back
	colLeft    = RGB{0, 60, 255}    // blue
	colRight   = RGB{0, 255, 180}   // cyan
	colConfirm = RGB{255, 255, 255} // white  -- "y", the quit confirmation

	// The play/cue/loop LEDs are single fixed-colour lamps: the firmware can
	// only switch them on. Any non-zero colour here means "lit"; the value is
	// what the chart prints, not what the lamp emits.
	colLamp = RGB{255, 255, 255}
)

// Doom is the deck as a Doom controller.
//
// Nearly every key below is one Chocolate Doom binds BY DEFAULT (arrows, Ctrl,
// Space, comma/period, Shift, the digits, Tab, Esc, Enter, y). That is
// deliberate: a vanilla config stores keys as DOOM's own internal codes -- not
// Linux ones, not ASCII -- so hand-authoring them is the easiest way to end up
// with a deck that presses nothing.
//
// The ONE exception is PLAY, on `]`, because Chocolate Doom leaves next-weapon
// unbound by default. ../../../doom/default.cfg binds it, and that file explains
// the encoding.
//
// The physical logic:
//   - The jog wheel is the turn axis, and TOUCHING it fires. A wheel is a rotary
//     control and turning is a rotary action; nothing else on the deck comes
//     close, and it is the one mapping here that is better than a real keyboard.
//   - The tempo fader is the throttle: a centre-detented linear fader is exactly
//     a forward/back speed lever, and it is the only analogue input left.
//   - Ring A has 7 populated nodes; Doom has 7 weapons. Ring B has 6; a menu
//     needs Esc, four cursors and a confirm. Both fit exactly.
func Doom() *Map {
	m := &Map{
		Name: "doom",
		Desc: "the deck as a Doom controller (Chocolate Doom default bindings)",
		Notes: map[byte]Binding{
			// FIRE IS THE JOG WHEEL'S TOUCH SENSOR: rest a hand on the platter
			// and the gun goes off. It reads as the deck's most Doom-like idea
			// -- the same hand that aims is the one that shoots -- with the
			// consequence that you cannot turn without firing. That is the
			// point, not an oversight.
			midimap.NoteJogTouch: {"fire", []uint16{uinput.KeyLeftCtrl}, RGB{}},

			// Play is freed by that, so it takes over weapon cycling.
			midimap.NotePlay: {"next weapon", []uint16{uinput.KeyRightBrace}, colLamp},
			midimap.NoteCue:  {"use / open door", []uint16{uinput.KeySpace}, colLamp},

			midimap.NoteLoopIn:  {"strafe left", []uint16{uinput.KeyComma}, colLamp},
			midimap.NoteLoopOut: {"strafe right", []uint16{uinput.KeyDot}, colLamp},
			// Reloop has a button but NO LED on the loop board, so this one
			// cannot be lit however much we would like it to be.
			midimap.NoteReloop: {"run (hold)", []uint16{uinput.KeyLeftShift}, RGB{}},

			// Menu confirm. The encoder is the deck's list-scrolling control
			// everywhere else, so pressing it is "select" here too.
			midimap.NoteEncSw: {"enter / select", []uint16{uinput.KeyEnter}, RGB{}},
		},

		Jog: JogMouse,

		// The track encoder is the deck's list-scrolling control everywhere
		// else, so here it scrolls the menus. Note that Doom's menu cursor and
		// its walk-forward/back are the SAME two keys, so in play (rather than
		// in a menu) a detent nudges you a step -- there is no third pair of
		// keys to give it, and the fader is the real throttle anyway.
		EncoderUp:   Binding{"menu up", []uint16{uinput.KeyUp}, RGB{}},
		EncoderDown: Binding{"menu down", []uint16{uinput.KeyDown}, RGB{}},

		FaderForward: Binding{"walk forward", []uint16{uinput.KeyUp}, RGB{}},
		FaderBack:    Binding{"walk backward", []uint16{uinput.KeyDown}, RGB{}},
		FaderRun:     Binding{"run", []uint16{uinput.KeyLeftShift}, RGB{}},
	}

	// Ring A: weapons 1..7, in weapon order round the ring.
	weapons := []Binding{
		{"weapon 1 fist / chainsaw", []uint16{uinput.Key1}, colFist},
		{"weapon 2 pistol", []uint16{uinput.Key2}, colPistol},
		{"weapon 3 shotgun", []uint16{uinput.Key3}, colShotgun},
		{"weapon 4 chaingun", []uint16{uinput.Key4}, colChain},
		{"weapon 5 rocket launcher", []uint16{uinput.Key5}, colRocket},
		{"weapon 6 plasma rifle", []uint16{uinput.Key6}, colPlasma},
		{"weapon 7 BFG 9000", []uint16{uinput.Key7}, colBFG},
	}
	for i, b := range weapons {
		if i >= midimap.PopulatedA {
			break
		}
		m.Notes[byte(midimap.PadABase+i)] = b
	}

	// Ring B: walking, and everything needed to drive the menus -- including
	// quitting the game, which needs a literal 'y' for "are you sure you want to
	// quit this great game?". Without that key you could start Doom from the
	// deck and never leave it without the panic chord.
	//
	// RED IS BACKWARD AND GREEN IS FORWARD, on the two adjacent pads, because
	// that is what those colours mean everywhere else in the world. It costs
	// nothing to honour: Doom's walk-forward/back and its menu cursor are the
	// SAME two keys, so one pad is both "walk forward" and "menu up" with no
	// ambiguity. Esc took the colour walking left behind rather than the pads
	// moving, so nothing on the deck changes position -- only meaning.
	ringB := []Binding{
		{"walk backward / menu down", []uint16{uinput.KeyDown}, colDown0},
		{"walk forward / menu up", []uint16{uinput.KeyUp}, colUp},
		{"menu / back", []uint16{uinput.KeyEsc}, colMenu},
		{"menu left / turn", []uint16{uinput.KeyLeft}, colLeft},
		{"menu right / turn", []uint16{uinput.KeyRight}, colRight},
		{"confirm quit (y)", []uint16{uinput.KeyY}, colConfirm},
	}
	for i, b := range ringB {
		if i >= midimap.PopulatedB {
			break
		}
		m.Notes[byte(midimap.PadBBase+i)] = b
	}
	return m
}
