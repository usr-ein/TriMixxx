// trimixxx-launchd is the deck's launch manager and its hands on the operating
// system.
//
// TWO JOBS, ONE DAEMON, BECAUSE THEY ARE ALMOST THE SAME JOB:
//
//  1. Mixxx cannot do system things itself: its controller scripts run in a bare
//     QJSEngine whose entire API is control values, timers and MIDI I/O -- no
//     file, process or network access -- and skins are just XML/QSS. MIDI is
//     therefore the only channel out of Mixxx, and SysEx is the only MIDI
//     message that carries an arbitrary payload. So this daemon exposes a
//     virtual MIDI port, Mixxx enables it as an ordinary controller, and
//     whatever Mixxx asks for in SysEx gets done here.
//
//  2. Something has to decide what the deck IS at boot -- Mixxx, Doom, or the
//     rescue console -- and that decision is made by holding a button on the
//     deck, which arrives as MIDI. This daemon already speaks MIDI, already runs
//     as root, and already gates the tty1 session that starts the app (its
//     readiness is what releases getty@tty1). It was one step away from being
//     the launch manager, so it is.
//
//     boot -> ttymidi -> launchd -> /run/trimixxx/mode -> getty@tty1 -> mixxx | doom | debug
//
// Wire format (see README.md):
//
//	F0 7D <opcode> [args] F7
//
// SECURITY: the opcode only ever *selects* an entry from a fixed table; no byte
// from the MIDI stream is passed to a command, and nothing goes through a shell.
// The trust boundary is the MIDI port itself: anything that can send SysEx to it
// can trigger these actions.
package main

import (
	"flag"
	"log"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/deck"
	"gitlab.com/gomidi/midi/v2/drivers/rtmididrv/imported/rtmidi"
)

// defaultPortName is what Mixxx lists under Preferences -> Controllers (Mixxx
// keys off the port name), and is also used as the ALSA client name.
//
// It is deliberately still "pi-midi-daemon" even though the daemon has been
// renamed: Mixxx keys its controller bindings off the DEVICE NAME (see
// mixxx_config/mixxx.cfg), so renaming this port would silently orphan the
// mapping and the skin's power menu would stop working.
const defaultPortName = "pi-midi-daemon"

// deckClientName is what ttymidi calls its ALSA client (its -n argument in
// pi_config/trimixxx-bridge.service). This is where the deck's buttons arrive.
const deckClientName = "TriMixxx"

// midiQueueSize is rtmidi's internal message queue. Commands are rare and tiny;
// this is just rtmidi's default order of magnitude.
const midiQueueSize = 1024

// deckEventBuffer is how many deck events can be in flight before they start
// being dropped. Deck traffic is bursty (one jog wheel spin is a stream of CCs)
// and this consumer only cares about buttons, so a drop under load costs
// nothing -- and dropping is far better than blocking rtmidi's callback thread.
const deckEventBuffer = 256

func main() {
	portName := flag.String("port", defaultPortName, "name of the virtual MIDI ports Mixxx talks to")
	dryRun := flag.Bool("dry-run", false, "log actions instead of running them")
	usbGlob := flag.String("usb-glob", defaultUSBGlob, "glob of USB mountpoints to watch")
	usbPoll := flag.Duration("usb-poll", defaultUSBPoll, "how often to check for USB mounts")

	deckPort := flag.String("deck-port", deckClientName, "ALSA port name carrying the deck's own MIDI (ttymidi)")
	modeFile := flag.String("mode-file", "/run/trimixxx/mode", "file naming the mode the deck should be in")
	sessionUser := flag.String("session-user", "sam1902", "user running the tty1 session, so it can request a mode")
	window := flag.Duration("select-window", defaultSelectWindow, "how long to watch the deck for the boot gesture")
	forceMode := flag.String("mode", "", "skip the boot gesture and use this mode (mixxx|doom|debug)")
	deckkeys := flag.String("deckkeys", "/usr/local/bin/trimixxx-deckkeys", "path to the MIDI->uinput bridge")
	gettyUnit := flag.String("getty-unit", "getty@tty1.service", "session unit to restart on a mode change")
	doomProc := flag.String("doom-process", "chocolate-doom", "process name that proves Doom is running")
	debugProc := flag.String("debug-process", "trimixxx-debug", "process name that proves the debug screen is running")
	flag.Parse()

	log.SetFlags(log.LstdFlags | log.Lmsgprefix)
	log.SetPrefix("trimixxx-launchd: ")

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
	// for from Mixxx, so without this the daemon is deaf. Timing clock and
	// active sensing stay ignored; they are noise here.
	if err := in.IgnoreTypes(false, true, true); err != nil {
		log.Fatalf("enable SysEx: %v", err)
	}

	if err := in.OpenVirtualPort(*portName); err != nil {
		log.Fatalf("create virtual MIDI port %q: %v", *portName, err)
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

	// The deck's own MIDI, straight from ttymidi and independent of Mixxx. This
	// is how the boot gesture and the panic chord are heard: at boot Mixxx does
	// not exist yet, and later it is busy being a DJ deck.
	events := make(chan deck.Event, deckEventBuffer)
	link := deck.Dial(*deckPort, "trimixxx-launchd", func(ev deck.Event) {
		select {
		case events <- ev:
		default: // full: this consumer only wants buttons, so a lost CC is free
		}
	}, log.Printf)
	defer link.Close()

	mgr := &manager{
		store:     newModeStore(*modeFile, *sessionUser),
		link:      link,
		dryRun:    *dryRun,
		deckkeys:  *deckkeys,
		deckPort:  *deckPort,
		gettyUnit: *gettyUnit,
		procs: map[Mode]string{
			ModeMixxx: "mixxx",
			ModeDoom:  *doomProc,
			ModeDebug: *debugProc,
		},
	}

	// Decide, and write the answer down. getty@tty1 -- and therefore whatever
	// app the deck is about to become -- is held back by this unit's readiness
	// gate until the mode file exists, so this runs with nothing on screen.
	if *forceMode != "" {
		mode, err := ParseMode(*forceMode)
		if err != nil {
			log.Fatalf("--mode: %v", err)
		}
		log.Printf("mode forced to %s by --mode; not watching the deck", mode)
		mgr.apply(mode, false)
	} else {
		mgr.boot(events, *window)
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

	// Deck events after the boot window: the panic chord back to Mixxx, and
	// mode requests written to the mode file by anything else.
	go mgr.run(events, done)

	// Mixxx's SysEx. Raw MIDI bytes, so a command arrives exactly as it went on
	// the wire. Registered last: nothing above should be able to fire mid-startup.
	if err := in.SetCallback(func(_ rtmidi.MIDIIn, msg []byte, _ float64) {
		dispatch(msg, *dryRun, mgr.request)
	}); err != nil {
		log.Fatalf("set callback: %v", err)
	}

	log.Printf("listening on virtual MIDI port %q, deck on %q, watching %q (mode=%s, dry-run=%v)",
		*portName, *deckPort, *usbGlob, mgr.Current(), *dryRun)

	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)
	<-sig
	// Stop the key bridge on the way out: it holds a kernel input device open,
	// and this is a shutdown, not a mode change.
	mgr.syncDeckkeys(ModeMixxx)
	time.Sleep(100 * time.Millisecond)
	log.Print("stopped")
}
