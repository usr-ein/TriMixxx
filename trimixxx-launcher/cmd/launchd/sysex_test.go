package main

import "testing"

// The wire format is a contract shared with Mixxx's mapping script, so pin it
// down: only our manufacturer ID is accepted, and framing is optional.
func TestParse(t *testing.T) {
	for _, tc := range []struct {
		name       string
		in         []byte
		wantOpcode byte
		wantArgs   string
		wantOurs   bool
	}{
		{"framed ping", []byte{0xF0, 0x7D, 0x00, 0xF7}, 0x00, "", true},
		{"framed shutdown", []byte{0xF0, 0x7D, 0x01, 0xF7}, 0x01, "", true},
		{"framed reboot", []byte{0xF0, 0x7D, 0x02, 0xF7}, 0x02, "", true},
		{"unframed shutdown", []byte{0x7D, 0x01}, 0x01, "", true},
		{"unknown opcode still ours", []byte{0xF0, 0x7D, 0x09, 0xF7}, 0x09, "", true},
		{"opcode with args", []byte{0xF0, 0x7D, 0x21, 'D', 'O', 'O', 'M', 0xF7}, 0x21, "DOOM", true},
		{"foreign manufacturer", []byte{0xF0, 0x42, 0x01, 0xF7}, 0, "", false},
		{"no opcode", []byte{0xF0, 0x7D, 0xF7}, 0, "", false},
		{"empty", nil, 0, "", false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			opcode, args, ours := parseFrame(tc.in)
			if ours != tc.wantOurs {
				t.Fatalf("parseFrame(% X) ours = %v, want %v", tc.in, ours, tc.wantOurs)
			}
			if !ours {
				return
			}
			if opcode != tc.wantOpcode {
				t.Fatalf("parseFrame(% X) opcode = %#02x, want %#02x", tc.in, opcode, tc.wantOpcode)
			}
			if string(args) != tc.wantArgs {
				t.Fatalf("parseFrame(% X) args = %q, want %q", tc.in, args, tc.wantArgs)
			}
			// The no-args helper must agree with the full parse.
			if op, ok := parse(tc.in); !ok || op != tc.wantOpcode {
				t.Fatalf("parse(% X) = %#02x,%v", tc.in, op, ok)
			}
		})
	}
}

// Mixxx addresses these opcodes by number, so they must not drift.
func TestActionTable(t *testing.T) {
	want := map[byte]string{0x00: "ping", 0x01: "shutdown", 0x02: "reboot"}
	if len(actions) != len(want) {
		t.Fatalf("action table has %d entries, want %d", len(actions), len(want))
	}
	for opcode, name := range want {
		act, ok := actions[opcode]
		if !ok {
			t.Fatalf("opcode %#02x (%s) missing", opcode, name)
		}
		if act.name != name {
			t.Errorf("opcode %#02x is %q, want %q", opcode, act.name, name)
		}
	}
	if actions[0x00].argv != nil {
		t.Error("ping must not run anything")
	}
	// The two command blocks must not overlap, or one would shadow the other.
	for opcode := range modeCommands {
		if _, clash := actions[opcode]; clash {
			t.Errorf("opcode %#02x is in both the action and the mode table", opcode)
		}
	}
}

// A mode switch replaces what is on the deck's screen, possibly mid-set. The
// magic is what stops a stray or corrupt SysEx from doing that -- the same
// precaution the firmware takes for its own reset command.
func TestModeCommandsNeedTheirMagic(t *testing.T) {
	for _, tc := range []struct {
		name string
		in   []byte
		want Mode
	}{
		{"doom with magic", []byte{0xF0, 0x7D, 0x21, 'D', 'O', 'O', 'M', 0xF7}, ModeDoom},
		{"mixxx with magic", []byte{0xF0, 0x7D, 0x20, 'M', 'I', 'X', 0xF7}, ModeMixxx},
		{"debug with magic", []byte{0xF0, 0x7D, 0x22, 'D', 'B', 'G', 0xF7}, ModeDebug},
		{"doom without magic", []byte{0xF0, 0x7D, 0x21, 0xF7}, ""},
		{"doom with wrong magic", []byte{0xF0, 0x7D, 0x21, 'D', 'O', 'O', 0xF7}, ""},
		{"doom with trailing junk", []byte{0xF0, 0x7D, 0x21, 'D', 'O', 'O', 'M', 0x01, 0xF7}, ""},
		{"mixxx magic on the doom opcode", []byte{0xF0, 0x7D, 0x21, 'M', 'I', 'X', 0xF7}, ""},
	} {
		t.Run(tc.name, func(t *testing.T) {
			var got Mode
			dispatch(tc.in, true, func(m Mode) { got = m })
			if got != tc.want {
				t.Fatalf("dispatch(% X) asked for %q, want %q", tc.in, got, tc.want)
			}
		})
	}
}

// The event frames are a contract with PiMidiDaemon.scripts.js, which matches on
// these exact bytes.
func TestEventFrame(t *testing.T) {
	for _, tc := range []struct {
		name   string
		opcode byte
		want   []byte
	}{
		{"usb mounted", evtUSBMounted, []byte{0xF0, 0x7D, 0x10, 0xF7}},
		{"usb unmounted", evtUSBUnmounted, []byte{0xF0, 0x7D, 0x11, 0xF7}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			got := eventFrame(tc.opcode)
			if string(got) != string(tc.want) {
				t.Fatalf("eventFrame(%#02x) = % X, want % X", tc.opcode, got, tc.want)
			}
			// Our own parser must agree, so a dump reads the same either way.
			if op, ours := parse(got); !ours || op != tc.opcode {
				t.Fatalf("parse(% X) = %#02x,%v", got, op, ours)
			}
		})
	}
}
