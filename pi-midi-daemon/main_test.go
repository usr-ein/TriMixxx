package main

import (
	"os"
	"path/filepath"
	"testing"
)

// The wire format is a contract shared with Mixxx's mapping script, so pin it
// down: only our manufacturer ID is accepted, and framing is optional.
func TestParse(t *testing.T) {
	for _, tc := range []struct {
		name       string
		in         []byte
		wantOpcode byte
		wantOurs   bool
	}{
		{"framed ping", []byte{0xF0, 0x7D, 0x00, 0xF7}, 0x00, true},
		{"framed shutdown", []byte{0xF0, 0x7D, 0x01, 0xF7}, 0x01, true},
		{"framed reboot", []byte{0xF0, 0x7D, 0x02, 0xF7}, 0x02, true},
		{"unframed shutdown", []byte{0x7D, 0x01}, 0x01, true},
		{"unknown opcode still ours", []byte{0xF0, 0x7D, 0x09, 0xF7}, 0x09, true},
		{"foreign manufacturer", []byte{0xF0, 0x42, 0x01, 0xF7}, 0, false},
		{"no opcode", []byte{0xF0, 0x7D, 0xF7}, 0, false},
		{"empty", nil, 0, false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			opcode, ours := parse(tc.in)
			if ours != tc.wantOurs {
				t.Fatalf("parse(% X) ours = %v, want %v", tc.in, ours, tc.wantOurs)
			}
			if ours && opcode != tc.wantOpcode {
				t.Fatalf("parse(% X) opcode = %#02x, want %#02x", tc.in, opcode, tc.wantOpcode)
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

// A plain directory must never look mounted -- dj-usb creates the slot dir
// before mounting onto it, so this is what stops a premature "mounted" event.
func TestIsMountpointPlainDir(t *testing.T) {
	dir := t.TempDir()
	sub := filepath.Join(dir, "DJ_USB_1")
	if err := os.Mkdir(sub, 0o755); err != nil {
		t.Fatal(err)
	}
	if isMountpoint(sub) {
		t.Error("a freshly created directory reported as a mountpoint")
	}
	if len(mountedSet(filepath.Join(dir, "DJ_USB_*"))) != 0 {
		t.Error("mountedSet counted an unmounted directory")
	}
}
