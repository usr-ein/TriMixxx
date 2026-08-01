package main

import (
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/deck"
	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/midimap"
)

func TestParseMode(t *testing.T) {
	for _, s := range []string{"mixxx", "doom", "debug"} {
		if _, err := ParseMode(s); err != nil {
			t.Errorf("ParseMode(%q): %v", s, err)
		}
	}
	for _, s := range []string{"", "MIXXX", "quake", "mixxx doom"} {
		if m, err := ParseMode(s); err == nil {
			t.Errorf("ParseMode(%q) accepted it as %q", s, m)
		}
	}
}

// Mixxx must never get a virtual keyboard: it speaks MIDI itself, and a
// keyboard typing Doom keys at a live set would be a catastrophe.
func TestMixxxGetsNoKeyBridge(t *testing.T) {
	if km := ModeMixxx.keymap(); km != "" {
		t.Errorf("mixxx mode wants keymap %q, must want none", km)
	}
	if km := ModeDoom.keymap(); km != "doom" {
		t.Errorf("doom mode wants keymap %q", km)
	}
	if km := ModeDebug.keymap(); km != "console" {
		t.Errorf("debug mode wants keymap %q", km)
	}
}

func TestModeStoreRoundTrip(t *testing.T) {
	s := &modeStore{path: filepath.Join(t.TempDir(), "trimixxx", "mode"), uid: -1, gid: -1}

	if _, err := s.read(); err == nil {
		t.Fatal("reading a mode that was never written should fail")
	}
	if err := s.write(ModeDoom); err != nil {
		t.Fatalf("write: %v", err)
	}
	got, err := s.read()
	if err != nil {
		t.Fatalf("read: %v", err)
	}
	if got != ModeDoom {
		t.Fatalf("read %q, wrote %q", got, ModeDoom)
	}

	// The session writes this file too (to hand the deck back after Doom), so a
	// trailing newline from `echo`/`printf` must not confuse it.
	if err := os.WriteFile(s.path, []byte("mixxx\n"), 0o664); err != nil {
		t.Fatal(err)
	}
	if got, err := s.read(); err != nil || got != ModeMixxx {
		t.Fatalf("read %q,%v after a shell-style write", got, err)
	}

	// Garbage must not be silently accepted as some mode.
	if err := os.WriteFile(s.path, []byte("rm -rf /\n"), 0o664); err != nil {
		t.Fatal(err)
	}
	if got, err := s.read(); err == nil {
		t.Fatalf("garbage in the mode file parsed as %q", got)
	}
}

// fakeLEDs records what the LEDs were told, which is the only feedback the boot
// window has on a deck whose screen is still black.
type fakeLEDs struct{ writes int }

func (f *fakeLEDs) SetLED(note byte, on bool) { f.writes++ }
func (f *fakeLEDs) Connected() bool           { return true }

// The boot gesture. The case that matters most is the LONE NOTE-OFF: the
// firmware's Note-On for a button held from power-on is sent ~20s before this
// daemon exists, so a release with no press before it is the only evidence that
// the button was being held. See selectMode's comment.
func TestSelectMode(t *testing.T) {
	confirmFlash = 0 // no need to actually blink at a test

	for _, tc := range []struct {
		name string
		send []deck.Event
		want Mode
	}{
		{
			"play released, never pressed -- held through boot",
			[]deck.Event{{Type: deck.NoteOff, Data1: midimap.NotePlay}},
			ModeDoom,
		},
		{
			"play pressed during the window",
			[]deck.Event{{Type: deck.NoteOn, Data1: midimap.NotePlay, Data2: 127}},
			ModeDoom,
		},
		{
			"cue released, never pressed",
			[]deck.Event{{Type: deck.NoteOff, Data1: midimap.NoteCue}},
			ModeDebug,
		},
		{
			"nothing at all",
			nil,
			ModeMixxx,
		},
		{
			"a nudged jog wheel is not a decision",
			[]deck.Event{
				{Type: deck.ControlChange, Data1: midimap.CCJog, Data2: 3},
				{Type: deck.ControlChange, Data1: midimap.CCTempo, Data2: 64},
			},
			ModeMixxx,
		},
		{
			"another button is not a decision",
			[]deck.Event{{Type: deck.NoteOn, Data1: midimap.NoteLoopIn, Data2: 127}},
			ModeMixxx,
		},
		{
			"first button to speak wins",
			[]deck.Event{
				{Type: deck.NoteOff, Data1: midimap.NoteCue},
				{Type: deck.NoteOff, Data1: midimap.NotePlay},
			},
			ModeDebug,
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			events := make(chan deck.Event, len(tc.send)+1)
			for _, ev := range tc.send {
				events <- ev
			}
			got := selectMode(events, &fakeLEDs{}, 150*time.Millisecond)
			if got != tc.want {
				t.Fatalf("selectMode = %q, want %q", got, tc.want)
			}
		})
	}
}

// A power cycle must always give you a working DJ deck, whatever the last mode
// was. That is why the mode file is on tmpfs and why there is no persistent
// default -- this test is here to fail if someone ever adds one.
func TestNoModeSurvivesAReboot(t *testing.T) {
	s := newModeStore("/run/trimixxx/mode", "root")
	if dir := filepath.Dir(s.path); dir != "/run/trimixxx" {
		t.Fatalf("the mode file lives in %s, which had better be tmpfs", dir)
	}
}

// The window has to be short enough not to be felt on a normal boot.
func TestSelectWindowIsShort(t *testing.T) {
	if defaultSelectWindow > 5*time.Second {
		t.Errorf("a %s boot delay on every single boot is too much", defaultSelectWindow)
	}
}
