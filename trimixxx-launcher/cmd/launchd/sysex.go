package main

import (
	"bytes"
	"log"
	"os/exec"
)

// sysExID is the SysEx manufacturer ID the MIDI spec reserves for
// non-commercial / educational use, so it can never collide with a real vendor.
const sysExID = 0x7D

// action is one thing Mixxx is allowed to ask for. argv is exec'd directly (no
// shell); a nil argv means "log only", which makes ping a harmless liveness probe.
type action struct {
	name string
	argv []string
}

// Commands, 0x0x block: things done to the machine.
var actions = map[byte]action{
	0x00: {name: "ping"},
	0x01: {name: "shutdown", argv: []string{"systemctl", "poweroff"}},
	0x02: {name: "reboot", argv: []string{"systemctl", "reboot"}},
}

// Event opcodes go the other way (daemon -> Mixxx), numbered from 0x10 to keep
// the two directions visibly apart in a MIDI dump. They carry no payload:
// "a stick appeared/vanished" is all Mixxx needs to kick off a Rekordbox rescan.
// Must match PiMidiDaemon.scripts.js.
const (
	evtUSBMounted   = 0x10
	evtUSBUnmounted = 0x11
)

// Mode commands, 0x2x block: things done to what the deck IS. Separate from the
// 0x0x actions because they are a different kind of dangerous -- one powers the
// machine off, the other replaces what is on screen mid-set.
//
// Each carries MAGIC, following the firmware's own precedent for its reset
// command (F0 7D 02 52 53 54 F7, "RST"): a stray or corrupt SysEx must not be
// able to drop a live set into Doom. A bare `F0 7D 21 F7` does nothing.
type modeCommand struct {
	mode  Mode
	magic []byte
}

var modeCommands = map[byte]modeCommand{
	0x20: {ModeMixxx, []byte("MIX")},
	0x21: {ModeDoom, []byte("DOOM")},
	0x22: {ModeDebug, []byte("DBG")},
}

// eventFrame builds the SysEx for an event: F0 7D <opcode> F7.
func eventFrame(opcode byte) []byte {
	return []byte{0xF0, sysExID, opcode, 0xF7}
}

// parseFrame returns the opcode and any argument bytes carried by a SysEx body,
// reporting false if the message is not addressed to us. It accepts the body
// with or without framing.
func parseFrame(data []byte) (byte, []byte, bool) {
	body := trimFraming(data)
	if len(body) < 2 || body[0] != sysExID {
		return 0, nil, false
	}
	return body[1], body[2:], true
}

// parse returns just the opcode, for callers that take no arguments.
func parse(data []byte) (byte, bool) {
	opcode, _, ok := parseFrame(data)
	return opcode, ok
}

// dispatch decodes one SysEx body and does what its opcode selects.
//
// SECURITY: the opcode only ever *selects* an entry from the fixed tables above.
// No byte from the MIDI stream is passed to a command and nothing goes through a
// shell -- the argument bytes are only ever compared against a constant.
func dispatch(data []byte, dryRun bool, setMode func(Mode)) {
	opcode, args, ours := parseFrame(data)
	if !ours {
		return
	}

	if cmd, ok := modeCommands[opcode]; ok {
		if !bytes.Equal(args, cmd.magic) {
			log.Printf("mode %s: ignoring, magic is % X not % X (%q)",
				cmd.mode, args, cmd.magic, cmd.magic)
			return
		}
		log.Printf("mode %s requested over SysEx", cmd.mode)
		setMode(cmd.mode)
		return
	}

	act, ok := actions[opcode]
	if !ok {
		log.Printf("ignoring unknown opcode %#02x", opcode)
		return
	}
	if act.argv == nil {
		log.Printf("%s", act.name)
		return
	}
	if dryRun {
		log.Printf("%s: dry-run, would run %v", act.name, act.argv)
		return
	}

	log.Printf("%s: running %v", act.name, act.argv)
	if out, err := exec.Command(act.argv[0], act.argv[1:]...).CombinedOutput(); err != nil {
		log.Printf("%s failed: %v: %s", act.name, err, out)
	}
}

// trimFraming drops the F0/F7 bytes so we accept the body whether or not the
// driver hands us the surrounding frame.
func trimFraming(b []byte) []byte {
	if len(b) > 0 && b[0] == 0xF0 {
		b = b[1:]
	}
	if len(b) > 0 && b[len(b)-1] == 0xF7 {
		b = b[:len(b)-1]
	}
	return b
}
