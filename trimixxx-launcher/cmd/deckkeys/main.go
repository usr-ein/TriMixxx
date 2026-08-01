// trimixxx-deckkeys makes the deck look like a keyboard and a mouse.
//
// It subscribes to the deck's MIDI (ttymidi's ALSA port, the same stream Mixxx
// uses) and replays it into the kernel through /dev/uinput as a virtual USB
// keyboard and a virtual mouse. Anything on the machine then sees ordinary
// input: X, the console, SDL. That is the entire trick that lets an
// UNMODIFIED Doom be played on a DJ deck -- no engine fork, no MIDI support in
// the game, no patched input layer. The mapping is data (internal/keymap), so
// changing what a pad does is a table edit, not a port.
//
// The launch manager starts and stops this: one instance while the deck is
// being Doom or the debug console, none at all while it is being Mixxx (Mixxx
// speaks MIDI itself, and a keyboard typing at a live set would be a disaster).
package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"sort"
	"syscall"
	"time"

	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/deck"
	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/keymap"
	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/midimap"
	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/uinput"
)

const deckEventBuffer = 1024

func main() {
	mapName := flag.String("map", "doom", "which mapping to run: "+fmt.Sprint(keymap.Names()))
	deckPort := flag.String("deck-port", "TriMixxx", "ALSA port name carrying the deck's MIDI")
	printMap := flag.Bool("print-map", false, "print the control chart and exit")
	dryRun := flag.Bool("dry-run", false, "log the keys instead of typing them (no /dev/uinput, no root)")

	// 1.8 px/tick, tuned on the deck across two passes (6 -> 3.6 -> 1.8). The
	// wheel is optical and high-resolution, so a small number goes a long way.
	jogScale := flag.Float64("jog-scale", 1.8, "mouse pixels per jog tick (turn speed)")
	// The wheel's quadrature counts the opposite way round from mouse X on this
	// hardware, so without this, turning it left looks right. Measured, not
	// guessed; -jog-invert=false undoes it if a wheel is ever rewired.
	jogInvert := flag.Bool("jog-invert", true, "match the wheel's direction to the turn direction")
	lights := flag.Bool("lights", true, "colour the deck's buttons to show what they do")
	faderDead := flag.Float64("fader-deadzone", 0.15, "fraction of fader travel around centre that means 'stand still'")
	faderRun := flag.Float64("fader-run", 0.60, "fraction of fader travel past which walking becomes running")
	faderInvert := flag.Bool("fader-invert", false, "swap which end of the tempo fader walks forward")
	flag.Parse()

	log.SetFlags(log.LstdFlags | log.Lmsgprefix)
	log.SetPrefix("trimixxx-deckkeys: ")

	km, err := keymap.For(*mapName)
	if err != nil {
		log.Fatal(err)
	}
	if *printMap {
		fmt.Print(km.Chart())
		return
	}

	t, err := newTypist(km, *dryRun)
	if err != nil {
		log.Fatal(err)
	}
	// Whatever happens, do not leave a key held down in the kernel: a stuck
	// Ctrl outlives this process and there is no keyboard on the deck to undo
	// it with.
	defer t.close()

	tr := &translator{
		km:          km,
		typist:      t,
		jogScale:    *jogScale,
		jogInvert:   *jogInvert,
		faderDead:   *faderDead,
		faderRun:    *faderRun,
		faderInvert: *faderInvert,
		tempoMSB:    midimap.TempoCenter >> 7,
		tempoLSB:    midimap.TempoCenter & 0x7F,
	}

	events := make(chan deck.Event, deckEventBuffer)
	link := deck.Dial(*deckPort, "trimixxx-deckkeys", func(ev deck.Event) {
		select {
		case events <- ev:
		default:
			// Dropping is right: every event here is either a button edge
			// (rare) or one tick of a wheel (worthless on its own).
		}
	}, log.Printf)
	defer link.Close()

	log.Printf("deck %q -> virtual keyboard%s, mapping %q (dry-run=%v)",
		*deckPort, map[bool]string{true: " + mouse", false: ""}[km.UsesMouse()], km.Name, *dryRun)

	// The deck is repainted on a timer rather than once at startup, and the
	// reason is a race worth remembering: switching modes starts this process
	// while the OUTGOING Mixxx is still being SIGTERMed, and Mixxx's controller
	// script blanks every ring LED on the way out (TriMixxx.shutdown, "Clear
	// every ring button LED"). A single paint at startup lands a second or two
	// before that and gets wiped -- which looks exactly like a deck that never
	// lit up. Repainting is also what heals an S3 that has been reset mid-game.
	//
	// It costs ~155 bytes every few seconds on a link that carries far more than
	// that from one flick of the jog wheel.
	painter := &painter{km: km, link: link, enabled: *lights}
	relight := time.NewTicker(relightEvery)
	defer relight.Stop()

	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)
	for {
		select {
		case ev := <-events:
			tr.handle(ev)

		case <-relight.C:
			painter.paint() // idempotent; a no-op if the link is down

		case s := <-sig:
			log.Printf("%v, releasing everything", s)
			// Before the deferred link.Close(), while the deck can still hear
			// us: hand the buttons back dark, so Mixxx does not inherit a deck
			// lit up like a Doom controller.
			painter.clear()
			return
		}
	}
}

// ---------------------------------------------------------------------------
//  painter -- the deck says what it does
// ---------------------------------------------------------------------------

// painter colours the deck's buttons from the loaded keymap, so a deck that is
// currently a Doom controller looks like one. Two kinds of light exist and they
// are not interchangeable:
//
//   - ring pads carry two WS2812s each and take a real colour, over SysEx;
//   - play/cue/loop are single fixed-colour lamps the firmware only switches,
//     so the best they can say is "this one does something";
//   - RELOOP has neither. There is no LED behind it on the loop board, so it
//     stays dark however it is bound. Nothing to fix in software.
type painter struct {
	km         *keymap.Map
	link       *deck.Link
	enabled    bool
	lastLogged string // so a repaint every few seconds does not fill the journal
}

func (p *painter) paint() { p.apply(false) }
func (p *painter) clear() { p.apply(true) }

// relightEvery is how often the deck is repainted.
//
// Measured, not assumed: a 143-byte burst of ring-LED SysEx sent in one write
// lights the deck exactly as well as the same messages spaced 25 ms apart, so
// there is no pacing here and none is needed. What the repaint is actually for
// is the Mixxx-shutdown race above.
const relightEvery = 3 * time.Second

func (p *painter) apply(dark bool) {
	if !p.enabled {
		return
	}
	// Sorted, so the deck paints round the ring in order and the log reads the
	// same way twice. Map iteration order would do neither.
	notes := make([]int, 0, len(p.km.Notes))
	for note := range p.km.Notes {
		notes = append(notes, int(note))
	}
	sort.Ints(notes)

	lit, lamps := 0, 0
	for _, n := range notes {
		note := byte(n)
		b := p.km.Notes[note]
		if !b.Color.Lit() {
			continue // nothing to say about this one, and nothing to clear
		}
		c := b.Color
		if dark {
			c = keymap.RGB{}
		}
		switch cmd, node, isPad := midimap.RingOf(note); {
		case isPad:
			p.link.SetPad(cmd, node, c.R, c.G, c.B)
			lit++
		case midimap.HasLamp(note):
			p.link.SetLED(note, !dark)
			lamps++
		}
		// Anything else has no LED behind it (reloop, the jog touch sensor).
	}
	// Only worth a line when it changes: this runs every few seconds.
	state := fmt.Sprintf("%d/%d", lit, lamps)
	if dark {
		state = "off"
	}
	if state != p.lastLogged {
		p.lastLogged = state
		what := "lit"
		if dark {
			what = "cleared"
		}
		log.Printf("lights: %s %d pads and %d lamps", what, lit, lamps)
	}
}

// ---------------------------------------------------------------------------
//  typist -- owns the virtual devices and what is currently held down
// ---------------------------------------------------------------------------

// tapHold is how long a "tap" holds its key. Zero would work -- the kernel and
// SDL both queue events rather than sampling them -- but a real key is down for
// tens of milliseconds, and some things do sample.
const tapHold = 15 * time.Millisecond

// output is a virtual device, as far as the typist cares. *uinput.Device is the
// real one; a nil output swallows everything, which is what --dry-run does (and
// what the tests replace with a recorder).
type output interface {
	Key(code uint16, down bool) error
	Move(dx, dy int32) error
	Close() error
}

type typist struct {
	kb      output
	mouse   output
	verbose bool

	// Reference-counted, because two controls legitimately hold the same key:
	// RELOOP is "run" and so is the far end of the tempo fader, both LEFTSHIFT.
	// Releasing one must not release the other's key out from under it.
	held map[uint16]int
}

func newTypist(km *keymap.Map, dry bool) (*typist, error) {
	t := &typist{verbose: dry, held: map[uint16]int{}}
	if dry {
		return t, nil // no devices at all: every key is just logged
	}
	kb, err := uinput.New("TriMixxx Deck Keyboard", km.Keys(), nil)
	if err != nil {
		return nil, err
	}
	t.kb = kb
	if km.UsesMouse() {
		// A pointer with buttons: libinput wants at least one to call it a
		// mouse, even though nothing here ever clicks.
		mouse, err := uinput.New("TriMixxx Deck Mouse",
			[]uint16{uinput.BtnLeft, uinput.BtnRight, uinput.BtnMiddle},
			[]uint16{uinput.RelX, uinput.RelY})
		if err != nil {
			_ = kb.Close()
			return nil, err
		}
		t.mouse = mouse
	}
	return t, nil
}

func (t *typist) hold(b keymap.Binding) {
	for _, k := range b.Keys {
		t.held[k]++
		if t.held[k] == 1 {
			t.key(k, true, b.Label)
		}
	}
}

func (t *typist) release(b keymap.Binding) {
	for _, k := range b.Keys {
		if t.held[k] == 0 {
			continue
		}
		t.held[k]--
		if t.held[k] == 0 {
			t.key(k, false, b.Label)
		}
	}
}

func (t *typist) tap(b keymap.Binding) {
	t.hold(b)
	time.Sleep(tapHold)
	t.release(b)
}

func (t *typist) key(code uint16, down bool, label string) {
	if t.verbose {
		verb := "release"
		if down {
			verb = "press  "
		}
		log.Printf("%s %-10s (%s)", verb, uinput.KeyName(code), label)
	}
	if t.kb == nil {
		return
	}
	if err := t.kb.Key(code, down); err != nil {
		log.Printf("key %s: %v", uinput.KeyName(code), err)
	}
}

func (t *typist) move(dx int32) {
	if t.verbose {
		log.Printf("mouse  %+d", dx)
	}
	if t.mouse == nil {
		return
	}
	if err := t.mouse.Move(dx, 0); err != nil {
		log.Printf("mouse move: %v", err)
	}
}

// close releases every held key BEFORE destroying the devices. Destroying them
// would also release the keys, but only for readers that handle device removal
// correctly -- and Doom, mid-frame, is not something to bet a stuck movement key
// on.
func (t *typist) close() {
	for code, n := range t.held {
		if n > 0 {
			t.key(code, false, "cleanup")
		}
	}
	if t.kb != nil {
		_ = t.kb.Close()
	}
	if t.mouse != nil {
		_ = t.mouse.Close()
	}
}

// ---------------------------------------------------------------------------
//  translator -- deck events to typist calls
// ---------------------------------------------------------------------------

type translator struct {
	km     *keymap.Map
	typist *typist

	jogScale  float64
	jogInvert bool
	jogRemain float64 // sub-pixel carry, so slow turns are smooth
	jogTicks  int     // for JogKeys mode

	faderDead   float64
	faderRun    float64
	faderInvert bool
	tempoMSB    byte
	tempoLSB    byte
	walking     keymap.Binding // what the fader currently holds, if anything
	running     bool
}

func (tr *translator) handle(ev deck.Event) {
	switch ev.Type {
	case deck.NoteOn:
		if b, ok := tr.km.Notes[ev.Data1]; ok {
			tr.typist.hold(b)
		}
	case deck.NoteOff:
		if b, ok := tr.km.Notes[ev.Data1]; ok {
			tr.typist.release(b)
		}
	case deck.ControlChange:
		switch ev.Data1 {
		case midimap.CCJog:
			tr.jog(midimap.JogDelta(ev.Data2))
		case midimap.CCEncoder:
			tr.encoder(midimap.EncoderDelta(ev.Data2))
		case midimap.CCTempo:
			tr.tempoMSB = ev.Data2
			tr.fader()
		case midimap.CCTempoLSB:
			tr.tempoLSB = ev.Data2
			tr.fader()
		}
	}
}

// jog turns wheel ticks into either pointer motion (Doom: this is the turn
// axis) or key taps (a text console, where a pointer is no use).
func (tr *translator) jog(delta int) {
	if delta == 0 {
		return
	}
	if tr.km.Jog == keymap.JogKeys {
		// The wheel is optical and edge-counted, so one key per tick would fly
		// past everything; the map says how many ticks make one keypress.
		per := max(tr.km.JogTicksPerKey, 1)
		tr.jogTicks += delta
		for tr.jogTicks >= per {
			tr.jogTicks -= per
			tr.typist.tap(tr.km.JogCW)
		}
		for tr.jogTicks <= -per {
			tr.jogTicks += per
			tr.typist.tap(tr.km.JogCCW)
		}
		return
	}

	// The wheel counts the opposite way round from mouse X on this deck, so
	// without the flip, turning it left looks right.
	if tr.jogInvert {
		delta = -delta
	}
	// Pointer motion is integral, but a slow turn is fractional -- carrying the
	// remainder is what keeps a gentle nudge from being rounded away to nothing.
	want := float64(delta)*tr.jogScale + tr.jogRemain
	whole := float64(int(want))
	tr.jogRemain = want - whole
	if whole != 0 {
		tr.typist.move(int32(whole))
	}
}

func (tr *translator) encoder(delta int) {
	if delta == 0 {
		return
	}
	// midimap.EncoderDelta returns +1 for the value the firmware documents as
	// "up" (1) and -1 for "down" (127), so this needs no opinion of its own.
	if delta > 0 {
		tr.typist.tap(tr.km.EncoderUp)
	} else {
		tr.typist.tap(tr.km.EncoderDown)
	}
}

// fader turns the 14-bit tempo fader into a throttle: neutral near the centre
// detent, walking past the deadzone, running past the run point.
//
// The deadzone is not optional. The fader is a bare pot read by an ADC, so its
// centre wanders by a few counts; without it the player would drift forward
// while standing still, which in Doom means walking into things and dying.
func (tr *translator) fader() {
	if !tr.km.UsesFader() {
		return
	}
	raw := int(tr.tempoMSB)<<7 | int(tr.tempoLSB)
	pos := float64(raw-midimap.TempoCenter) / float64(midimap.TempoCenter) // -1..+1
	if tr.faderInvert {
		pos = -pos
	}

	// Hysteresis: once moving, it takes rather less to keep moving than it took
	// to start. Otherwise a fader resting exactly on the threshold stutters
	// between walk and stand.
	dead := tr.faderDead
	if tr.walking.Bound() {
		dead *= 0.7
	}

	var want keymap.Binding
	switch {
	case pos > dead:
		want = tr.km.FaderForward
	case pos < -dead:
		want = tr.km.FaderBack
	}

	if want.Label != tr.walking.Label {
		if tr.walking.Bound() {
			tr.typist.release(tr.walking)
		}
		if want.Bound() {
			tr.typist.hold(want)
		}
		tr.walking = want
	}

	run := tr.walking.Bound() && abs(pos) >= tr.faderRun && tr.km.FaderRun.Bound()
	if run != tr.running {
		if run {
			tr.typist.hold(tr.km.FaderRun)
		} else {
			tr.typist.release(tr.km.FaderRun)
		}
		tr.running = run
	}
}

func abs(f float64) float64 {
	if f < 0 {
		return -f
	}
	return f
}
