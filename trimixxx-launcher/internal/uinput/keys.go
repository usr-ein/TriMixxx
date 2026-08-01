package uinput

import "strconv"

// Linux key codes, from <linux/input-event-codes.h>. Only the ones the deck can
// produce are listed -- a device may only emit codes it declared at creation, so
// this list is deliberately the vocabulary of the keymaps and nothing more.
//
// These are NOT ASCII and not X keysyms: they are kernel scancodes, translated
// to keysyms by whatever keymap X or the console has loaded. That translation is
// why KeyA below is 30 and not 'a'.
const (
	KeyEsc   uint16 = 1
	Key1     uint16 = 2
	Key2     uint16 = 3
	Key3     uint16 = 4
	Key4     uint16 = 5
	Key5     uint16 = 6
	Key6     uint16 = 7
	Key7     uint16 = 8
	Key8     uint16 = 9
	Key9     uint16 = 10
	Key0     uint16 = 11
	KeyMinus uint16 = 12
	KeyEqual uint16 = 13

	KeyBackspace  uint16 = 14
	KeyTab        uint16 = 15
	KeyLeftBrace  uint16 = 26
	KeyRightBrace uint16 = 27
	KeyEnter      uint16 = 28
	KeyLeftCtrl   uint16 = 29
	KeyLeftShift  uint16 = 42
	KeyLeftAlt    uint16 = 56
	KeySpace      uint16 = 57

	KeyY uint16 = 21
	KeyN uint16 = 49

	KeyComma uint16 = 51
	KeyDot   uint16 = 52
	KeySlash uint16 = 53

	KeyF1 uint16 = 59
	KeyF2 uint16 = 60
	KeyF3 uint16 = 61
	KeyF4 uint16 = 62
	KeyF5 uint16 = 63
	KeyF6 uint16 = 64
	KeyF7 uint16 = 65

	KeyHome     uint16 = 102
	KeyUp       uint16 = 103
	KeyPageUp   uint16 = 104
	KeyLeft     uint16 = 105
	KeyRight    uint16 = 106
	KeyEnd      uint16 = 107
	KeyDown     uint16 = 108
	KeyPageDown uint16 = 109
)

// Weapons are the digit keys Doom uses to select weapons 1..7 -- exactly as
// many as ring A has populated nodes, which is a coincidence too good to waste.
var Weapons = []uint16{Key1, Key2, Key3, Key4, Key5, Key6, Key7}

var keyNames = map[uint16]string{
	KeyEsc: "esc", Key1: "1", Key2: "2", Key3: "3", Key4: "4", Key5: "5",
	Key6: "6", Key7: "7", Key8: "8", Key9: "9", Key0: "0",
	KeyMinus: "-", KeyEqual: "=", KeyBackspace: "backspace", KeyTab: "tab",
	KeyLeftBrace: "[", KeyRightBrace: "]", KeyEnter: "enter",
	KeyLeftCtrl: "ctrl", KeyLeftShift: "shift", KeyLeftAlt: "alt", KeySpace: "space",
	KeyY: "y", KeyN: "n", KeyComma: ",", KeyDot: ".", KeySlash: "/",
	KeyF1: "F1", KeyF2: "F2", KeyF3: "F3", KeyF4: "F4", KeyF5: "F5", KeyF6: "F6", KeyF7: "F7",
	KeyHome: "home", KeyUp: "up", KeyPageUp: "pgup", KeyLeft: "left",
	KeyRight: "right", KeyEnd: "end", KeyDown: "down", KeyPageDown: "pgdn",
}

// KeyName renders a key code for a log line or the printed control chart.
func KeyName(code uint16) string {
	if n, ok := keyNames[code]; ok {
		return n
	}
	return "key" + strconv.Itoa(int(code))
}
