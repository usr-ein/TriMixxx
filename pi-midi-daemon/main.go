// pi-midi-daemon performs privileged system actions on the Pi on behalf of Mixxx.
//
// Mixxx cannot do system things itself: its controller scripts run in a bare
// QJSEngine whose entire API is control values, timers and MIDI I/O -- no file,
// process or network access -- and skins are just XML/QSS. MIDI is therefore the
// only channel out of Mixxx, and SysEx is the only MIDI message that carries an
// arbitrary payload. This daemon exposes a virtual MIDI port, Mixxx enables it as
// an ordinary controller, and whatever Mixxx asks for in SysEx gets done here.
//
// Wire format (see README.md):
//
//	F0 7D <opcode> F7
//
// SECURITY: the opcode only ever *selects* an entry from the fixed table below;
// no byte from the MIDI stream is passed to a command, and nothing goes through a
// shell. The trust boundary is the MIDI port itself: anything that can send SysEx
// to it can trigger these actions.
package main

import (
	"flag"
	"log"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"

	"gitlab.com/gomidi/midi/v2/drivers/rtmididrv/imported/rtmidi"
)

// sysExID is the SysEx manufacturer ID the MIDI spec reserves for
// non-commercial / educational use, so it can never collide with a real vendor.
const sysExID = 0x7D

// defaultPortName is what Mixxx lists under Preferences -> Controllers (Mixxx
// keys off the port name), and is also used as the ALSA client name.
const defaultPortName = "pi-midi-daemon"

// midiQueueSize is rtmidi's internal message queue. Commands are rare and tiny;
// this is just rtmidi's default order of magnitude.
const midiQueueSize = 1024

// action is one thing Mixxx is allowed to ask for. argv is exec'd directly (no
// shell); a nil argv means "log only", which makes ping a harmless liveness probe.
type action struct {
	name string
	argv []string
}

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

// Where ../mixxx_config/dj-usb mounts DJ sticks (slots DJ_USB_1 / DJ_USB_2),
// and how often to look. Seconds of latency are irrelevant for a library
// refresh, and polling is deliberate -- see watchUSBMounts.
const (
	defaultUSBGlob = "/media/DJ_USB_*"
	defaultUSBPoll = 2 * time.Second
)

func main() {
	portName := flag.String("port", defaultPortName, "name of the virtual MIDI ports Mixxx talks to")
	dryRun := flag.Bool("dry-run", false, "log actions instead of running them")
	usbGlob := flag.String("usb-glob", defaultUSBGlob, "glob of USB mountpoints to watch")
	usbPoll := flag.Duration("usb-poll", defaultUSBPoll, "how often to check for USB mounts")
	flag.Parse()

	log.SetFlags(log.LstdFlags | log.Lmsgprefix)
	log.SetPrefix("pi-midi-daemon: ")

	// The ALSA *client* is named here, and the port below gets the same name, so
	// `aconnect -l` and any host show one consistent name -- ttymidi does the
	// same thing for the same reason. (gomidi's higher-level OpenVirtualIn calls
	// rtmidi.NewMIDIInDefault(), which would leave the client as "RtMidi Input
	// Client" and only name the port, hence going one layer down here.)
	in, err := rtmidi.NewMIDIIn(rtmidi.APIUnspecified, *portName, midiQueueSize)
	if err != nil {
		log.Fatalf("open MIDI: %v", err)
	}
	// Close() is the whole teardown: it unregisters the callback, closes the
	// port AND calls rtmidi_in_free(). Adding a Destroy() would free a second
	// time and abort the process on exit.
	defer in.Close()

	// rtmidi drops SysEx by default -- and SysEx is the only thing we listen
	// for, so without this the daemon is deaf. Timing clock and active sensing
	// stay ignored; they are noise here.
	if err := in.IgnoreTypes(false, true, true); err != nil {
		log.Fatalf("enable SysEx: %v", err)
	}

	if err := in.OpenVirtualPort(*portName); err != nil {
		log.Fatalf("create virtual MIDI port %q: %v", *portName, err)
	}

	// Raw MIDI bytes, so a command arrives exactly as it went on the wire.
	if err := in.SetCallback(func(_ rtmidi.MIDIIn, msg []byte, _ float64) {
		dispatch(msg, *dryRun)
	}); err != nil {
		log.Fatalf("set callback: %v", err)
	}

	// Outbound port, same name again, for events we report to Mixxx. rtmidi
	// makes one ALSA client per instance, so this is a second client that also
	// happens to be called pi-midi-daemon; Mixxx pairs an in and an out device
	// of the same name into a single controller, which is what we want.
	out, err := rtmidi.NewMIDIOut(rtmidi.APIUnspecified, *portName)
	if err != nil {
		log.Fatalf("open MIDI out: %v", err)
	}
	defer out.Close() // frees, like the in port -- never also Destroy()
	if err := out.OpenVirtualPort(*portName); err != nil {
		log.Fatalf("create virtual MIDI out port %q: %v", *portName, err)
	}

	done := make(chan struct{})
	defer close(done)
	go watchUSBMounts(*usbGlob, *usbPoll, func(opcode byte, path string) {
		what := "mounted"
		if opcode == evtUSBUnmounted {
			what = "unmounted"
		}
		log.Printf("usb %s: %s (sending %#02x)", what, path, opcode)
		if err := out.SendMessage(eventFrame(opcode)); err != nil {
			log.Printf("send usb event: %v", err)
		}
	}, done)

	log.Printf("listening on virtual MIDI port %q, watching %q (dry-run=%v)",
		*portName, *usbGlob, *dryRun)

	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)
	<-sig
	log.Print("stopped")
}

// eventFrame builds the SysEx for an event: F0 7D <opcode> F7.
func eventFrame(opcode byte) []byte {
	return []byte{0xF0, sysExID, opcode, 0xF7}
}

// watchUSBMounts polls for DJ_USB_* mountpoints and reports each one appearing
// and disappearing exactly once.
//
// Polling rather than inotify/udev is deliberate. mixxx_config/dj-usb does
// `mkdir` and only THEN `mount`, so inotify's IN_CREATE would fire on an empty
// directory before the filesystem is there, and Mixxx would rescan nothing.
// inotify cannot see the mount itself. Testing st_dev on a timer sees the state
// that actually matters, needs no dependency, and a couple of seconds of lag is
// nothing for a library refresh.
func watchUSBMounts(glob string, every time.Duration, emit func(opcode byte, path string), done <-chan struct{}) {
	// Seed from whatever is already mounted: this unit starts before Mixxx, so
	// an event fired now would go nowhere anyway.
	seen := mountedSet(glob)
	ticker := time.NewTicker(every)
	defer ticker.Stop()

	for {
		select {
		case <-done:
			return
		case <-ticker.C:
			cur := mountedSet(glob)
			for path := range cur {
				if !seen[path] {
					emit(evtUSBMounted, path)
				}
			}
			for path := range seen {
				if !cur[path] {
					emit(evtUSBUnmounted, path)
				}
			}
			seen = cur
		}
	}
}

// mountedSet returns the glob matches that are actually mounted filesystems.
func mountedSet(glob string) map[string]bool {
	set := map[string]bool{}
	matches, err := filepath.Glob(glob)
	if err != nil {
		return set // only ever a malformed pattern
	}
	for _, path := range matches {
		if isMountpoint(path) {
			set[path] = true
		}
	}
	return set
}

// isMountpoint reports whether path is the root of a mounted filesystem, by
// comparing its device to its parent's. "The directory exists" is not the same
// thing: dj-usb creates the slot dir before mounting onto it.
func isMountpoint(path string) bool {
	fi, err := os.Stat(path)
	if err != nil {
		return false
	}
	parent, err := os.Stat(filepath.Dir(path))
	if err != nil {
		return false
	}
	a, aOK := fi.Sys().(*syscall.Stat_t)
	b, bOK := parent.Sys().(*syscall.Stat_t)
	return aOK && bOK && a.Dev != b.Dev
}

// parse returns the opcode carried by a SysEx body, reporting false if the
// message is not addressed to us. It accepts the body with or without framing.
func parse(data []byte) (byte, bool) {
	body := trimFraming(data)
	if len(body) < 2 || body[0] != sysExID {
		return 0, false
	}
	return body[1], true
}

// dispatch decodes one SysEx body and runs the action its opcode selects.
func dispatch(data []byte, dryRun bool) {
	opcode, ours := parse(data)
	if !ours {
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
