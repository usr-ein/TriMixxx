package main

import (
	"context"
	"errors"
	"fmt"
	"log"
	"os"
	"os/exec"
	"os/user"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/deck"
	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/midimap"
)

// Mode is what the deck is being right now. Exactly one is true at a time, and
// it is chosen at boot -- see selectMode.
type Mode string

const (
	ModeMixxx Mode = "mixxx" // the DJ deck. The default, and what a power cycle always gives you.
	ModeDoom  Mode = "doom"  // DOOM (1993).
	ModeDebug Mode = "debug" // the rescue console. Deliberately empty for now.
)

// ParseMode validates a mode name coming from anywhere outside this process.
func ParseMode(s string) (Mode, error) {
	switch m := Mode(s); m {
	case ModeMixxx, ModeDoom, ModeDebug:
		return m, nil
	}
	return "", fmt.Errorf("unknown mode %q (have: mixxx, doom, debug)", s)
}

// keymap is the deckkeys mapping this mode wants, or "" for none. Mixxx needs
// none: it speaks MIDI itself, and a virtual keyboard typing at it would be
// actively harmful.
func (m Mode) keymap() string {
	switch m {
	case ModeDoom:
		return "doom"
	case ModeDebug:
		return "console"
	}
	return ""
}

// modeStore is /run/trimixxx/mode: the one thing that says which mode the deck
// is in.
//
// It lives on tmpfs ON PURPOSE. A wedged Doom, a debug screen someone walked
// away from, a mode set by a botched experiment -- none of them survive a power
// cycle, so the deck cannot be bricked into anything but Mixxx. There is no
// persistent "default mode" setting and there should not be one.
//
// It is also the request channel, in both directions and for anything that can
// write a file:
//
//	launchd  writes it  -> the tty1 session reads it and starts that app
//	anything writes it  -> launchd notices and makes reality match
//
// which is how the Doom wrapper hands the deck back on exit, and how a human
// over ssh can say `echo doom | sudo tee /run/trimixxx/mode`.
type modeStore struct {
	path string
	uid  int // session user, so the tty1 session can write a request
	gid  int
}

func newModeStore(path, sessionUser string) *modeStore {
	s := &modeStore{path: path, uid: -1, gid: -1}
	u, err := user.Lookup(sessionUser)
	if err != nil {
		// Not fatal: everything still works, the session just cannot request a
		// mode change (the Doom wrapper's exit path). Worth a loud line.
		log.Printf("mode: no user %q (%v); %s will be root-only", sessionUser, err, filepath.Dir(path))
		return s
	}
	s.uid, _ = strconv.Atoi(u.Uid)
	s.gid, _ = strconv.Atoi(u.Gid)
	return s
}

// ensure creates the directory, group-writable by the session user.
func (s *modeStore) ensure() error {
	dir := filepath.Dir(s.path)
	if err := os.MkdirAll(dir, 0o775); err != nil {
		return err
	}
	if err := os.Chmod(dir, 0o775); err != nil { // MkdirAll respects umask; this does not
		return err
	}
	if s.uid >= 0 {
		if err := os.Chown(dir, 0, s.gid); err != nil {
			log.Printf("mode: chown %s: %v", dir, err)
		}
	}
	return nil
}

// read returns the current mode, or an error if the file is absent or garbage.
func (s *modeStore) read() (Mode, error) {
	b, err := os.ReadFile(s.path)
	if err != nil {
		return "", err
	}
	return ParseMode(strings.TrimSpace(string(b)))
}

// write replaces the file atomically, so a reader either sees the old mode or
// the new one -- never a half-written word. The session reads this file at
// exactly the wrong moment (during a restart) by design.
func (s *modeStore) write(m Mode) error {
	if err := s.ensure(); err != nil {
		return err
	}
	tmp := s.path + ".tmp"
	if err := os.WriteFile(tmp, []byte(string(m)+"\n"), 0o664); err != nil {
		return err
	}
	if err := os.Chmod(tmp, 0o664); err != nil {
		return err
	}
	if s.uid >= 0 {
		if err := os.Chown(tmp, 0, s.gid); err != nil {
			log.Printf("mode: chown %s: %v", tmp, err)
		}
	}
	return os.Rename(tmp, s.path)
}

// ---------------------------------------------------------------------------
//  Boot-time mode selection
// ---------------------------------------------------------------------------

// selectWindow is how long the deck is asked "which mode?" at boot, and
// blinkPeriod how fast the play/cue LEDs flash while it is asking.
const (
	defaultSelectWindow = 3 * time.Second
	blinkPeriod         = 150 * time.Millisecond
)

// ledWriter is the deck's LEDs, as far as mode selection cares. An interface so
// the gesture can be tested without an ALSA sequencer to talk to.
type ledWriter interface {
	SetLED(note byte, on bool)
}

// deckLink is what the manager needs from the deck link.
type deckLink interface {
	ledWriter
	Connected() bool
}

// confirmFlash is one on/off step of the acknowledgement blink; tests zero it.
var confirmFlash = 80 * time.Millisecond

// selectMode watches the deck for the boot gesture and returns the mode chosen.
//
// WHY THIS IS NOT SIMPLY "IS THE BUTTON DOWN":
//
// The firmware sends a Note-On when a button becomes held -- including one
// already held when the S3 boots, since its debounced level starts false. But
// the S3 is up about half a second after power-on and this daemon about twenty
// seconds after that, so THAT Note-On is long gone: ttymidi did not exist to
// carry it, and nothing retransmits. There is no "read current button state"
// message in the firmware's SysEx table to ask with, either.
//
// What survives is the RELEASE. A button held from power-on through the whole
// boot sends its Note-Off the moment the finger lifts -- and a Note-Off with no
// Note-On before it means exactly one thing: "this was already down when I
// started listening". So the gesture is *hold from power-on, let go when the
// LEDs start blinking*, and either edge selects: a lone Note-Off is the hold, a
// Note-On is someone pressing during the window. Nobody touches the deck at boot
// otherwise, so there is no ambiguity to resolve.
//
// The blinking LEDs are not decoration -- they are the only way to know the
// window is open, on a unit whose screen is still black.
func selectMode(events <-chan deck.Event, link ledWriter, window time.Duration) Mode {
	// The two buttons that mean something here, and what they choose.
	choices := map[byte]Mode{
		midimap.NotePlay: ModeDoom,
		midimap.NoteCue:  ModeDebug,
	}

	log.Printf("hold PLAY for Doom, CUE for the debug console (%s)...", window)
	blink := time.NewTicker(blinkPeriod)
	defer blink.Stop()
	deadline := time.NewTimer(window)
	defer deadline.Stop()

	lit := false
	defer func() {
		link.SetLED(midimap.NotePlay, false)
		link.SetLED(midimap.NoteCue, false)
	}()

	for {
		select {
		case ev := <-events:
			mode, ours := choices[ev.Data1]
			if !ours || ev.Type == deck.ControlChange {
				continue // a nudged jog wheel is not a decision
			}
			how := "released -- so it was held through boot"
			if ev.Type == deck.NoteOn {
				how = "pressed"
			}
			log.Printf("boot gesture: %s %s -> %s", midimap.NoteName(ev.Data1), how, mode)
			confirm(link, ev.Data1)
			return mode

		case <-blink.C:
			lit = !lit
			link.SetLED(midimap.NotePlay, lit)
			link.SetLED(midimap.NoteCue, lit)

		case <-deadline.C:
			return ModeMixxx
		}
	}
}

// confirm flashes the chosen button's LED, so the choice is acknowledged before
// the screen comes up (Doom takes a few seconds to appear).
func confirm(link ledWriter, note byte) {
	for range 3 {
		link.SetLED(note, true)
		time.Sleep(confirmFlash)
		link.SetLED(note, false)
		time.Sleep(confirmFlash)
	}
}

// ---------------------------------------------------------------------------
//  Running the chosen mode
// ---------------------------------------------------------------------------

// panicChord is loop-in + loop-out held together: strafe left and strafe right
// at the same time. Chosen because it is the one combination a player would
// never hold -- the two cancel out -- and because it needs no button that Doom
// or the debug screen wants for anything urgent. It is the way back to Mixxx
// when the screen is showing something that cannot be quit.
const (
	panicHold  = 2 * time.Second
	chordCheck = 250 * time.Millisecond
	// requestPoll is how often an outside write to the mode file is noticed.
	// Polling, not inotify -- same reasoning as the USB watcher: it is a couple
	// of file reads a second, it cannot miss a state (only a transient), and it
	// has no dependency.
	requestPoll = 1 * time.Second
)

// manager owns the mode: it decides it at boot, applies it, and keeps reality
// matching it afterwards.
type manager struct {
	store     *modeStore
	link      deckLink
	dryRun    bool
	deckkeys  string // path to the trimixxx-deckkeys binary
	deckPort  string // the ALSA port name it should listen to
	gettyUnit string
	procs     map[Mode]string // mode -> process name that proves it is running

	mu       sync.Mutex
	current  Mode
	stopKeys context.CancelFunc
}

// boot decides the mode and applies it, WITHOUT restarting the session: at this
// point the tty1 session does not exist yet -- this unit's readiness gate is
// what releases it -- so writing the file is the whole job.
func (m *manager) boot(events <-chan deck.Event, window time.Duration) Mode {
	// A mode file that already exists means this is a restart of the daemon,
	// not a boot of the deck (the file is on tmpfs). Re-running the window then
	// would be wrong twice over: it would stall the restart for three seconds,
	// and it could yank a running Mixxx into something else.
	if mode, err := m.store.read(); err == nil {
		log.Printf("mode: %s already selected (daemon restart, not a fresh boot)", mode)
		m.apply(mode, false)
		return mode
	}

	// Give the deck link a moment to come up before opening the window --
	// otherwise the first second of it is spent deaf, and the LEDs do not blink
	// so the DJ has nothing to release on.
	for range 20 {
		if m.link.Connected() {
			break
		}
		time.Sleep(100 * time.Millisecond)
	}
	if !m.link.Connected() {
		log.Print("mode: deck link is not up; booting Mixxx without asking")
		m.apply(ModeMixxx, false)
		return ModeMixxx
	}

	mode := selectMode(events, m.link, window)
	m.apply(mode, false)
	return mode
}

// run keeps the mode honest: it watches the deck for the panic chord and the
// mode file for outside requests. Both funnel into request().
func (m *manager) run(events <-chan deck.Event, done <-chan struct{}) {
	chord := time.NewTicker(chordCheck)
	defer chord.Stop()
	poll := time.NewTicker(requestPoll)
	defer poll.Stop()

	var loopIn, loopOut bool
	var since time.Time

	for {
		select {
		case <-done:
			return

		case ev := <-events:
			if ev.Type == deck.ControlChange {
				continue
			}
			down := ev.Type == deck.NoteOn
			switch ev.Data1 {
			case midimap.NoteLoopIn:
				loopIn = down
			case midimap.NoteLoopOut:
				loopOut = down
			default:
				continue
			}
			if loopIn && loopOut {
				since = time.Now()
			} else {
				since = time.Time{}
			}

		case <-chord.C:
			// Only outside Mixxx: in Mixxx these are real loop buttons, and
			// holding both is a normal thing to do while setting a loop.
			if since.IsZero() || m.Current() == ModeMixxx {
				continue
			}
			if time.Since(since) >= panicHold {
				since = time.Time{}
				log.Print("panic chord (loop in + loop out held): back to Mixxx")
				m.request(ModeMixxx)
			}

		case <-poll.C:
			want, err := m.store.read()
			if err != nil || want == m.Current() {
				continue
			}
			log.Printf("mode: %s requested by something else (mode file)", want)
			m.apply(want, true)
		}
	}
}

// request switches mode, restarting the tty1 session if it is running the wrong
// thing. Safe to call from any goroutine.
func (m *manager) request(mode Mode) {
	if mode == m.Current() {
		return
	}
	m.apply(mode, true)
}

// apply makes the world match a mode: record it, write it down, run (or stop)
// the key bridge, and restart the session if it is showing the wrong app.
func (m *manager) apply(mode Mode, restartSession bool) {
	if err := m.store.write(mode); err != nil {
		log.Printf("mode: write %s: %v", m.store.path, err)
	}
	m.mu.Lock()
	m.current = mode
	m.mu.Unlock()
	log.Printf("mode: %s", mode)

	m.syncDeckkeys(mode)

	if !restartSession {
		return
	}
	// Restart ONLY if something is definitely running the wrong app. "Nothing
	// is running" means a session is already in flight -- which is the normal
	// case when Doom has just exited and asked for Mixxx on its way out: the
	// session ends, getty respawns it, and it reads the file we just wrote.
	// Restarting on top of that would kill the session that is already doing
	// the right thing.
	switch app := m.runningApp(); {
	case app == "":
		log.Print("mode: no app running; letting the session come up on its own")
	case app == mode:
		log.Printf("mode: %s already running", app)
	default:
		log.Printf("mode: %s is running, restarting the session for %s", app, mode)
		m.restartSession()
	}
}

// Current is the mode as last applied.
func (m *manager) Current() Mode {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.current
}

// runningApp reports which mode's app is on screen, or "" if none is.
func (m *manager) runningApp() Mode {
	for mode, proc := range m.procs {
		if proc == "" {
			continue
		}
		if err := exec.Command("pgrep", "-x", proc).Run(); err == nil {
			return mode
		}
	}
	return ""
}

// restartSession bounces the autologin session on tty1. That unit's drop-in
// (pi_config/getty-tty1-stop-mixxx.conf) asks Mixxx to quit and waits for it
// before the scope is torn down, so this is also the clean way to stop Mixxx.
func (m *manager) restartSession() {
	argv := []string{"systemctl", "restart", m.gettyUnit}
	if m.dryRun {
		log.Printf("dry-run, would run %v", argv)
		return
	}
	if out, err := exec.Command(argv[0], argv[1:]...).CombinedOutput(); err != nil {
		log.Printf("restart %s failed: %v: %s", m.gettyUnit, err, out)
	}
}

// syncDeckkeys starts, stops or replaces the MIDI->uinput bridge so that it is
// running with the right keymap for this mode -- and NOT running in Mixxx mode,
// where a virtual keyboard typing Doom keys at a DJ set would be a disaster.
func (m *manager) syncDeckkeys(mode Mode) {
	m.mu.Lock()
	stop := m.stopKeys
	m.stopKeys = nil
	m.mu.Unlock()
	if stop != nil {
		stop()
	}

	km := mode.keymap()
	if km == "" {
		return
	}
	if m.dryRun {
		log.Printf("dry-run, would run %s --map %s", m.deckkeys, km)
		return
	}

	ctx, cancel := context.WithCancel(context.Background())
	m.mu.Lock()
	m.stopKeys = cancel
	m.mu.Unlock()
	go m.superviseDeckkeys(ctx, km)
}

// superviseDeckkeys keeps the bridge alive for as long as the mode wants it. It
// is a separate process rather than a goroutine so that a bug in it cannot take
// the launch manager -- and therefore the way back to Mixxx -- down with it.
func (m *manager) superviseDeckkeys(ctx context.Context, km string) {
	for ctx.Err() == nil {
		cmd := exec.CommandContext(ctx, m.deckkeys, "--map", km, "--deck-port", m.deckPort)
		cmd.Stdout, cmd.Stderr = os.Stdout, os.Stderr
		// SIGTERM, not the default SIGKILL: deckkeys releases every held key on
		// the way out, and a key left down in the kernel would outlive it.
		cmd.Cancel = func() error { return cmd.Process.Signal(syscall.SIGTERM) }
		cmd.WaitDelay = 3 * time.Second

		log.Printf("deckkeys: starting (--map %s)", km)
		err := cmd.Run()
		if ctx.Err() != nil {
			log.Print("deckkeys: stopped")
			return
		}
		if err != nil && !errors.Is(err, context.Canceled) {
			log.Printf("deckkeys: exited: %v", err)
		}
		select {
		case <-ctx.Done():
			return
		case <-time.After(2 * time.Second):
		}
	}
}
