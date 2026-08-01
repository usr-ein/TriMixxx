// Package deck is a client of the real deck's MIDI stream.
//
// The deck's controls arrive on the Pi as ALSA MIDI, injected by ttymidi (the
// `trimixxx-bridge` service) from the S3's UART. ttymidi creates its ports with
// SND_SEQ_PORT_CAP_SUBS_READ / SUBS_WRITE -- i.e. *subscribable*, not exclusive
// -- so we can listen to the deck alongside Mixxx and light its LEDs alongside
// Mixxx, without either side knowing the other is there.
//
// That is what lets the launch manager read the play button at boot (before
// Mixxx exists) and still read a panic chord later (while Mixxx is running).
package deck

import (
	"bytes"
	"fmt"
	"os"
	"strings"
	"sync"
	"time"

	"github.com/usr-ein/TriMixxx/trimixxx-launcher/internal/midimap"
	"gitlab.com/gomidi/midi/v2/drivers/rtmididrv/imported/rtmidi"
)

// EventType is the kind of MIDI message a deck control produced. The deck only
// ever sends these three; anything else is dropped by Decode.
type EventType uint8

const (
	NoteOn EventType = iota
	NoteOff
	ControlChange
)

func (t EventType) String() string {
	switch t {
	case NoteOn:
		return "note-on"
	case NoteOff:
		return "note-off"
	case ControlChange:
		return "cc"
	}
	return "?"
}

// Event is one decoded deck message. Data1 is the note or CC number, Data2 the
// velocity or CC value.
type Event struct {
	Type  EventType
	Data1 byte
	Data2 byte
}

func (e Event) String() string { return fmt.Sprintf("%s %d %d", e.Type, e.Data1, e.Data2) }

// Decode turns a raw MIDI message from the deck into an Event, reporting false
// for anything that is not a channel-voice message on the deck's channel.
//
// Note-On with velocity 0 is treated as Note-Off: that is the standard MIDI
// idiom, and while this firmware does send a real 0x80, accepting both costs
// nothing and means a running-status stream would still read correctly.
func Decode(msg []byte) (Event, bool) {
	if len(msg) < 3 {
		return Event{}, false
	}
	if msg[0]&0x0F != midimap.Channel {
		return Event{}, false
	}
	switch msg[0] & 0xF0 {
	case 0x90:
		if msg[2] == 0 {
			return Event{NoteOff, msg[1], 0}, true
		}
		return Event{NoteOn, msg[1], msg[2]}, true
	case 0x80:
		return Event{NoteOff, msg[1], msg[2]}, true
	case 0xB0:
		return Event{ControlChange, msg[1], msg[2]}, true
	}
	return Event{}, false
}

// seqClients is where ALSA lists its sequencer clients by name. See alive().
const seqClients = "/proc/asound/seq/clients"

// reconnectPoll is how often a dropped link is retried, and how often a live one
// is checked for its peer having gone away.
const reconnectPoll = 2 * time.Second

// midiQueueSize is rtmidi's internal message queue; deck traffic is small.
const midiQueueSize = 1024

// Link is a live connection to the deck: events out of it, LED writes into it.
// It reconnects on its own, because `trimixxx-bridge.service` is Restart=always
// -- if the serial link hiccups, ttymidi comes back with a *different* ALSA
// client number and every port index shifts, so anything holding an old handle
// is deaf until it re-enumerates by name.
//
// The alternative -- letting systemd restart *us* when the bridge restarts
// (PartOf=) -- would be simpler but wrong: it would also tear down the virtual
// port Mixxx is attached to, and Mixxx enumerates MIDI devices exactly once, at
// startup. Losing the deck for two seconds is recoverable; losing Mixxx's
// controller until the next boot is not.
type Link struct {
	portMatch  string
	clientName string
	onEvent    func(Event)
	logf       func(string, ...any)

	mu   sync.Mutex
	in   rtmidi.MIDIIn
	out  rtmidi.MIDIOut
	done chan struct{}
	stop sync.Once
}

// Dial starts maintaining a connection to the first MIDI port whose name
// contains portMatch (ttymidi names both of its ports after its -n argument,
// "TriMixxx"). It returns immediately: the deck may not be up yet, and callers
// have other things to do. onEvent is called from rtmidi's callback thread.
func Dial(portMatch, clientName string, onEvent func(Event), logf func(string, ...any)) *Link {
	l := &Link{
		portMatch:  portMatch,
		clientName: clientName,
		onEvent:    onEvent,
		logf:       logf,
		done:       make(chan struct{}),
	}
	go l.maintain()
	return l
}

// maintain connects, and keeps the connection matching reality.
func (l *Link) maintain() {
	ticker := time.NewTicker(reconnectPoll)
	defer ticker.Stop()
	// Retries happen every couple of seconds forever, so the same failure must
	// not be logged every couple of seconds forever: say it once, then stay
	// quiet until something actually changes.
	var lastErr string
	for {
		switch {
		case !l.Connected() && alive(l.portMatch):
			if err := l.connect(); err != nil {
				if err.Error() != lastErr {
					l.logf("deck: connect: %v (retrying every %s)", err, reconnectPoll)
					lastErr = err.Error()
				}
				l.disconnect()
			} else {
				lastErr = ""
				l.logf("deck: listening to %q", l.portMatch)
			}
		case l.Connected() && !alive(l.portMatch):
			l.logf("deck: %q went away", l.portMatch)
			l.disconnect()
		}
		select {
		case <-l.done:
			l.disconnect()
			return
		case <-ticker.C:
		}
	}
}

// alive reports whether an ALSA sequencer client with this name exists.
//
// This reads /proc rather than asking rtmidi to re-enumerate, deliberately: an
// open rtmidi input owns a sequencer handle that its callback thread is using,
// and PortCount/PortName would poke that same handle from this goroutine. The
// proc file races with nothing, costs a read, and is the same liveness test the
// deck's systemd units already use.
//
// It fails OPEN -- no /proc/asound (a dev machine, macOS) must never be read as
// "the deck vanished", or the link would flap on a laptop.
func alive(name string) bool {
	b, err := os.ReadFile(seqClients)
	if err != nil {
		return true
	}
	return bytes.Contains(b, []byte(name))
}

func (l *Link) connect() error {
	in, err := rtmidi.NewMIDIIn(rtmidi.APIUnspecified, l.clientName, midiQueueSize)
	if err != nil {
		return fmt.Errorf("open MIDI in: %w", err)
	}
	l.mu.Lock()
	l.in = in
	l.mu.Unlock()

	// The deck never sends SysEx, timing clock or active sensing, so keep all
	// three ignored (rtmidi's default) -- nothing to buffer, nothing to filter.
	if err := in.IgnoreTypes(true, true, true); err != nil {
		return fmt.Errorf("ignore types: %w", err)
	}
	port, err := findPort(in, l.portMatch)
	if err != nil {
		return err
	}
	if err := in.OpenPort(port, l.clientName+" deck-in"); err != nil {
		return fmt.Errorf("subscribe to %q: %w", l.portMatch, err)
	}
	if err := in.SetCallback(func(_ rtmidi.MIDIIn, msg []byte, _ float64) {
		if ev, ok := Decode(msg); ok {
			l.onEvent(ev)
		}
	}); err != nil {
		return fmt.Errorf("set callback: %w", err)
	}

	// The outbound half is for LEDs only. If it fails we keep the inbound half:
	// reading the deck is the job, lighting it is the courtesy.
	out, err := rtmidi.NewMIDIOut(rtmidi.APIUnspecified, l.clientName)
	if err != nil {
		l.logf("deck: no LED output: %v", err)
		return nil
	}
	port, err = findPort(out, l.portMatch)
	if err != nil {
		l.logf("deck: no LED output: %v", err)
		_ = out.Close()
		return nil
	}
	if err := out.OpenPort(port, l.clientName+" deck-out"); err != nil {
		l.logf("deck: no LED output: %v", err)
		_ = out.Close()
		return nil
	}
	l.mu.Lock()
	l.out = out
	l.mu.Unlock()
	return nil
}

// findPort returns the index of the first port whose name contains match.
// rtmidi's ALSA names look like "TriMixxx:TriMixxx 128:0", so a substring test
// on the client name is what identifies it -- indices shift whenever any client
// on the machine comes or goes.
func findPort(m rtmidi.MIDI, match string) (int, error) {
	n, err := m.PortCount()
	if err != nil {
		return 0, fmt.Errorf("count ports: %w", err)
	}
	for i := range n {
		name, err := m.PortName(i)
		if err != nil {
			continue
		}
		if strings.Contains(name, match) {
			return i, nil
		}
	}
	return 0, fmt.Errorf("no MIDI port matching %q among %d ports", match, n)
}

func (l *Link) disconnect() {
	l.mu.Lock()
	in, out := l.in, l.out
	l.in, l.out = nil, nil
	l.mu.Unlock()
	// Close() is the whole teardown for both: it closes the port AND frees the
	// rtmidi instance. Never also call Destroy() -- that frees a second time and
	// aborts the process.
	if in != nil {
		_ = in.Close()
	}
	if out != nil {
		_ = out.Close()
	}
}

// Connected reports whether deck events are currently arriving.
func (l *Link) Connected() bool {
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.in != nil
}

// Send writes a raw MIDI message to the deck. It is a no-op (not an error) when
// the link is down: every caller is doing something cosmetic with it, and none
// of them should have to care.
func (l *Link) Send(msg []byte) {
	l.mu.Lock()
	out := l.out
	l.mu.Unlock()
	if out == nil {
		return
	}
	if err := out.SendMessage(msg); err != nil {
		l.logf("deck: send % X: %v", msg, err)
	}
}

// SetLED lights or clears the LED behind a named button. The firmware reads
// Note-On velocity > 0 as on and Note-Off as off (onMidiFromMixxx in main.cpp),
// on the same note the button itself sends.
func (l *Link) SetLED(note byte, on bool) {
	if on {
		l.Send([]byte{0x90 | midimap.Channel, note, 127})
		return
	}
	l.Send([]byte{0x80 | midimap.Channel, note, 0})
}

// SetPad sets one ring pad's colour, on both of its LEDs.
//
// cmd is the ring's SysEx command (midimap.SysExCmdRingA / RingB). Each 8-bit
// channel travels as TWO data bytes, high nibble first, because SysEx payload
// bytes must be 7-bit and a single one would cap every channel at 127 -- half
// brightness. Six colour bytes means "one colour, mirrored onto both LEDs";
// twelve would set them independently. See MidiMap.hpp.
func (l *Link) SetPad(cmd, node byte, r, g, b byte) {
	l.Send(padFrame(cmd, node, r, g, b))
}

// padFrame builds that message. Split out from SetPad so the encoding can be
// tested without an ALSA sequencer -- getting a nibble the wrong way round
// produces a colour that is merely wrong, which is exactly the kind of bug that
// survives review and then needs hardware to find.
func padFrame(cmd, node byte, r, g, b byte) []byte {
	return []byte{
		0xF0, midimap.SysExMfrID, cmd, node,
		r >> 4, r & 0x0F,
		g >> 4, g & 0x0F,
		b >> 4, b & 0x0F,
		0xF7,
	}
}

// Close stops reconnecting and drops the link.
func (l *Link) Close() {
	l.stop.Do(func() { close(l.done) })
}
