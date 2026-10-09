package main

import (
	"bytes"
	"log"
	"os/exec"
)

// sysExID is the SysEx manufacturer ID the MIDI spec reserves for
// non-commercial / educational use, so it can never collide with a real vendor.
const sysExID = 0x7D

// action is one thing Mixxx is allowed to ask for. Its steps are exec'd
// directly (no shell), in order, and the first that fails ends it; no steps
// means "log only", which makes ping a harmless liveness probe. An action with
// magic does nothing unless the message carries exactly those bytes after the
// opcode, as a mode command must (below).
type action struct {
	name  string
	magic []byte
	steps [][]string
}

// Commands, 0x0x block: things done to the machine.
var actions = map[byte]action{
	0x00: {name: "ping"},
	0x01: {name: "shutdown", steps: [][]string{{"systemctl", "poweroff"}}},
	0x02: {name: "reboot", steps: [][]string{{"systemctl", "reboot"}}},
	// The other A/B slot, as a trial (Diagnostics' RESTART). RAUC has the
	// backend arm the firmware's tryboot for it (pi_config/rauc/rpi-tryboot
	// set-primary), and the health check then keeps it or falls back to the
	// slot committed now (pi_config/trimixxx-health). A plain reboot would
	// start the committed slot again, and a `reboot N` would start the other
	// slot without a trial, which the health check takes for a fallback. If
	// RAUC can't mark it, nothing reboots: on a dev card there is no RAUC.
	//
	// Magic, as the modes carry: this changes what the deck boots, so a stray
	// or corrupt SysEx must not be able to.
	0x03: {name: "switch-slot", magic: []byte("SLOT"), steps: [][]string{
		{"rauc", "status", "mark-active", "other"},
		{"systemctl", "reboot"},
	}},
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

// stepRunner runs one step of the action *name*: execStep, or dryRunStep
// under --dry-run.
type stepRunner func(name string, argv []string) ([]byte, error)

// execStep runs a step as this daemon, root, with no shell.
func execStep(name string, argv []string) ([]byte, error) {
	log.Printf("%s: running %v", name, argv)
	return exec.Command(argv[0], argv[1:]...).CombinedOutput()
}

// dryRunStep only says what would run.
func dryRunStep(name string, argv []string) ([]byte, error) {
	log.Printf("%s: dry-run, would run %v", name, argv)
	return nil, nil
}

// dispatch decodes one SysEx body and does what its opcode selects.
//
// SECURITY: the opcode only ever *selects* an entry from the fixed tables above.
// No byte from the MIDI stream is passed to a command and nothing goes through a
// shell -- the argument bytes are only ever compared against a constant.
func dispatch(data []byte, run stepRunner, setMode func(Mode)) {
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
	if act.magic != nil && !bytes.Equal(args, act.magic) {
		log.Printf("%s: ignoring, magic is % X not % X (%q)", act.name, args, act.magic, act.magic)
		return
	}
	perform(act, run)
}

// perform runs an action's steps in order and stops at the first that fails:
// a slot switch whose slot RAUC couldn't mark must not go on to reboot.
func perform(act action, run stepRunner) {
	if len(act.steps) == 0 {
		log.Printf("%s", act.name)
		return
	}
	for _, argv := range act.steps {
		if out, err := run(act.name, argv); err != nil {
			log.Printf("%s failed: %v: %s", act.name, err, out)
			return
		}
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
