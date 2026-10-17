// ===========================================================================
//  TriMixxx.scripts.js  -  Mixxx controller script for the TriMixxx S3 deck.
//
//  Pairs with TriMixxx.midi.xml and mirrors the firmware's MIDI contract in
//  lib/PiLink/MidiMap.hpp EXACTLY. One deck = MIDI channel 1 = [Channel1].
//  If you change a MIDI address in MidiMap.hpp, change it here AND in the .xml.
// ===========================================================================
var TriMixxx = {};

// ---- constants (mirror MidiMap.hpp) ----
TriMixxx.RING_A_N      = 7;     // ring A buttons populated today (for the shutdown LED clear)
TriMixxx.RING_B_N      = 6;     // ring B buttons populated today
TriMixxx.DECK          = "[Channel1]";
TriMixxx.DECK_NUM      = 1;      // [Channel1] = deck 1 (single fixed deck)
TriMixxx.JOG_TICKS_REV = 12960; // JogWheel::TICKS_PER_REV (full-quad ticks / rev)
TriMixxx.RATE_RANGE    = 0.06;  // tempo fader span at boot = +/-6%
TriMixxx.RATE_RANGES   = [0.06, 0.10, 0.16, 1.0]; // A1 cycles these: +/-6, 10, 16, Wide (100%)
TriMixxx.LONG_PRESS_MS = 600;   // hold past this and a button takes its second meaning
// A second BACK inside this window is the switch chattering, not a DJ. The A7
// node debounces in hardware (10k + 100nF into a Schmitt trigger) and the ring
// protocol forbids a software debounce on top of it -- so this is not one: it
// is a guard on the *action*, at the layer that decides what BACK means, and
// popping two levels from one press is the thing it refuses.
//
// 200 ms: an order of magnitude longer than contact chatter, and short enough
// that unwinding a deep stack by tapping still works. Raise it if a stray pop
// still gets through; lower it if deliberate taps start being eaten.
TriMixxx.BACK_DEBOUNCE_MS = 200;

// ---- this deck's wiring ----------------------------------------------------
// Ring pads are numbered by their place in the daisy chain, and PLAY/CUE by the
// GPIO they land on, so a deck wired in a different order sends the right MIDI
// for the wrong button. trimixxx2 is one: an eighth pad sits third in ring A,
// pushing the loop pads and BACK one node down; hot cues 1-4 come in reversed;
// PLAY and CUE are swapped. The fix lives here and in the XML, not in the S3,
// so the firmware stays one build for every deck.
//
// `buttons` maps a canonical note (MidiMap.hpp) to the note this deck actually
// sends for that control, `lights` to the note -- for a ring pad, the node --
// its LED actually answers to. Both are empty on a standard deck.
// `jogReversed` is a jog encoder wired the other way round (trimixxx2's is).
// The marked line is REPLACED at deploy time (units/apply.py, from deploy step
// 005_config) from units/<hostname>.json, which renumbers the XML's notes from the same file, so
// that file is the one thing in this folder that differs from deck to deck.
TriMixxx.UNIT = {deck: "", buttons: {}, lights: {}, jogReversed: false}; // @unit-wiring

// Physical note -> canonical, for the handlers that read meaning off the note.
TriMixxx.UNIT_IN = (function() {
    var inverse = {};
    for (var canonical in TriMixxx.UNIT.buttons) {
        inverse[TriMixxx.UNIT.buttons[canonical]] = Number(canonical);
    }
    return inverse;
}());
TriMixxx.canonicalNote = function(note) {
    var canonical = TriMixxx.UNIT_IN[note];
    return canonical === undefined ? note : canonical;
};
// The node a ring-LED SysEx must address to light a canonical pad.
TriMixxx.physicalNode = function(cmd, node) {
    var base = (cmd === 0x03) ? 0x43 : 0x00; // ring B's notes start at 0x43, ring A's at 0
    var note = TriMixxx.UNIT.lights[base + node];
    return note === undefined ? node : note - base;
};

// ---- SORT LED ----------------------------------------------------------
// The pad's colour says which field the browser is sorting by and its
// brightness says which direction. Both come from the browser rather than from
// state kept here: [Browser],sort_column is an INDEX into WDeckSortMenu's field
// list and sort_order is 0 or 1, so this table only has to agree with that
// list's order -- and nothing here has to know a column name.
//
// This replaced a cycling state machine that lived in this script and drove the
// sort itself. The sort is the browser's now; a script that also kept its own
// idea of it could only ever disagree.
TriMixxx.SORT_COLOURS = [
    null,                 // 0 Default -- no sort, pad dark
    [0, 90, 255],         // 1 BPM        blue
    [150, 0, 255],        // 2 Key        purple
    [200, 200, 200],      // 3 Title      white
    [255, 110, 0],        // 4 Artist     amber
    [0, 200, 0],          // 5 Genre      green
    [0, 180, 180],        // 6 Album      teal
    [255, 60, 120],       // 7 Date added pink
    [180, 180, 0],        // 8 Label      olive
    [120, 120, 255],      // 9 Year       periwinkle
    [255, 160, 0],        // 10 Duration  orange
    [255, 0, 0]           // 11 Rating    red
];
// Ascending is the dim half of each pair, descending the bright one.
TriMixxx.SORT_DIM      = 0.28;
TriMixxx.SORT_BRIGHT   = 1.0;
TriMixxx.sortHoldTimer = 0;

// ---- KEY SYNC ----------------------------------------------------------
// The same pad over the deck. Purple, because that is the Key colour in the
// sort table above and the pad should mean one thing whichever view it is in.
//
// Three states and they are not the usual on/off: bright while the deck is out
// of the track's own key, dim when a press would put it on the master's key,
// dark when a press would do nothing. The middle one is the one that matters --
// it is how a DJ knows the CDJ has taken master and the button has become
// live, without looking at the screen.
//
// "Dim" here is dimmer than any other pad on the deck, and it is done twice
// over: 15% intensity AND only one of the node's two LEDs. Scaling alone did
// not get there -- these are bright parts behind a diffuser, and two of them at
// a low duty cycle still read as a lit button rather than as a hint. Half-lit
// is a state nothing else on the deck uses, which is exactly why it works: it
// says "this button has become available" without competing with the buttons
// that are actually doing something.
TriMixxx.C_KEY_SYNC    = [150, 0, 255];
TriMixxx.KEY_SYNC_DIM  = 0.15;

// ---- Ring button LED palette. Entries only need correct hue RATIOS -- dim()
//      normalizes each to full intensity, then scales by BRIGHTNESS. ----
TriMixxx.BRIGHTNESS = 0.8;          // 0..1 default for indicator LEDs (per-call override below)
// The whole ring, last thing before a colour leaves for the S3: every level in
// this file -- indicators, SORT/KEY SYNC, hot cues, the boot sweep -- is relative
// to full, and this scales all of them together, so "the pads are too bright"
// is one number that keeps every ratio above.
//
// It is how bright the rings LOOK, not their duty cycle. A WS2812 is linear in
// duty and nothing between here and it (S3, ring node) corrects for gamma, but
// the eye is not linear: 0.8 of the duty cycle reads as barely 10% dimmer, which
// is what the first attempt at this produced -- no visible change. So the
// level is converted through the usual 2.2 gamma: 0.8 looks ~20% dimmer and is
// ~61% of the duty cycle. 0.8 since 2026-10-01, when the rings read too bright.
TriMixxx.RING_LEVEL = 0.8;
TriMixxx.RING_DUTY = Math.pow(TriMixxx.RING_LEVEL, 2.2);
TriMixxx.C_OFF    = [0, 0, 0];
TriMixxx.C_DIM_W  = [24, 24, 24];   // A7 back: dim white
TriMixxx.C_RED    = [160, 0, 0];
TriMixxx.C_RED_HI = [255, 0, 0];
TriMixxx.C_ORANGE = [180, 45, 0];
TriMixxx.C_GREEN  = [0, 170, 0];
TriMixxx.C_YELLOW = [170, 130, 0];
// A1 tempo-range colour keyed by the current rateRange value.
TriMixxx.RANGE_LED = [[0.06, TriMixxx.C_GREEN], [0.10, TriMixxx.C_YELLOW],
                      [0.16, TriMixxx.C_ORANGE], [1.0, TriMixxx.C_RED]];
TriMixxx.conns     = [];    // LED engine connections (disconnected on shutdown)
TriMixxx.slipTimer = 0;     // slip-on flash timer id (0 = not flashing)
TriMixxx.slipPhase = false;
// ---- Jog feel ----------------------------------------------------------
// Pitch-bend sensitivity, as a fraction of raw hardware movement: 0.3 = 70%
// less sensitive. Bend only -- scratch is deliberately left at 1:1 below.
TriMixxx.JOG_BEND_SENSITIVITY = 0.3;

// Scratch gearing. 1.0 = the platter tracks vinyl 1:1, which is the only ratio
// that feels physically right: Mixxx needs ~0.56 rev/s to reach 1x playback, so
// gearing this down means turning proportionally harder before the track moves
// at all, which reads as lag rather than as calm. Lower it only if you want
// scratch geared down and accept that trade.
TriMixxx.JOG_SCRATCH_RATIO = 1.0;

// Alpha-beta filter for scratch. Mixxx runs this on a 1ms timer and uses its
// predicted velocity *as* the scratch rate: m_v += residual * beta / dt, with
// dt = 1ms. With no input the velocity decays by (1 - beta) per tick, so:
//
//     stop time constant (ms) ~= 1 / beta
//
// That rule predicted the tuning history exactly: Mixxx's stock alpha/32
// (0.0039) gives ~256ms and drifted forever; alpha/8 (0.0156) gives ~64ms and
// still had a felt delay. 1/16 gives ~16ms, which is below perception.
//
// beta cannot be raised alone: an alpha-beta filter is critically damped near
// beta ~= alpha^2 / (2 - alpha), and going far above that makes it under-damped
// so it overshoots and rings. Solving for beta = 1/16 puts alpha ~= 1/3, which
// is why both moved together.
//
// To tune: pick the stop time you want, set beta = 1 / (that many ms), then set
// alpha so the critical-damping relation roughly holds. Jitter means alpha/beta
// are too high for the encoder's noise; sluggishness means too low.
TriMixxx.JOG_ALPHA = 1.0 / 3;
TriMixxx.JOG_BETA  = 1.0 / 16;

TriMixxx.scratching = false;

// ---- startup rainbow-wave animation (Mixxx-driven; no firmware support) ----
// Sweeps a rainbow "comet" through both rings in the deck's layout order, using
// ONLY the existing per-node ring-LED SysEx: F0 7D <cmd> <node> <R,G,B as two
// nibbles hi-first each> F7, where cmd 0x01 = ring A, 0x03 = ring B.

// [SysEx cmd, node] in play order: B1 B2 B3 B4 B5 A6 A5 A4 A3 A1 A2 A7 B6.
TriMixxx.INTRO_SEQ = [
    [0x03, 0], [0x03, 1], [0x03, 2], [0x03, 3], [0x03, 4],
    [0x01, 5], [0x01, 4], [0x01, 3], [0x01, 2],
    [0x01, 0], [0x01, 1], [0x01, 6],
    [0x03, 5]
];

// hue 0..255 -> [r, g, b] on the colour wheel (full saturation/value).
TriMixxx.hueWheel = function(pos) {
    pos = 255 - (pos & 0xFF);
    if (pos < 85)  { return [255 - pos * 3, 0, pos * 3]; }
    if (pos < 170) { pos -= 85; return [0, pos * 3, 255 - pos * 3]; }
    pos -= 170;
    return [pos * 3, 255 - pos * 3, 0];
};

// One node -> both its LEDs, via the 6-nibble short-form ring-LED SysEx.
TriMixxx.level = function(v) { return Math.round(v * TriMixxx.RING_DUTY); };
TriMixxx.ringLed = function(cmd, node, r, g, b) {
    if (TriMixxx.lightsOff) { return; } // the screen is off: every light with it
    node = TriMixxx.physicalNode(cmd, node);
    r = TriMixxx.level(r); g = TriMixxx.level(g); b = TriMixxx.level(b);
    midi.sendSysexMsg(
        [0xF0, 0x7D, cmd, node,
            (r >> 4) & 0xF, r & 0xF, (g >> 4) & 0xF, g & 0xF, (b >> 4) & 0xF, b & 0xF, 0xF7], 11);
};

// One node -> its two LEDs SEPARATELY, via the 12-nibble long form of the same
// command. The firmware tells the two apart by length alone (MidiMap.hpp:
// SYSEX_RING_LED_ARGS_ONE / _TWO), and a wrong length is dropped in silence, so
// the count below is not decoration: 4 header bytes + 12 nibbles + F7 = 17.
//
// Half-lighting a node is the dimmest a pad on this deck can be, well below
// what scaling one colour can reach. See KEY_SYNC_DIM.
TriMixxx.ringLedPair = function(cmd, node, a, b) {
    if (TriMixxx.lightsOff) { return; }
    node = TriMixxx.physicalNode(cmd, node);
    // Copies: callers pass shared palette constants, which must not dim in place.
    a = a.map(TriMixxx.level); b = b.map(TriMixxx.level);
    midi.sendSysexMsg(
        [0xF0, 0x7D, cmd, node,
            (a[0] >> 4) & 0xF, a[0] & 0xF, (a[1] >> 4) & 0xF, a[1] & 0xF, (a[2] >> 4) & 0xF, a[2] & 0xF,
            (b[0] >> 4) & 0xF, b[0] & 0xF, (b[1] >> 4) & 0xF, b[1] & 0xF, (b[2] >> 4) & 0xF, b[2] & 0xF,
            0xF7], 17);
};

// Sweep the comet once (~1s) with a repeating timer, then stop and leave the
// sequence blank so Mixxx repaints the real LED state.
TriMixxx.playIntro = function(onDone) {
    var seq = TriMixxx.INTRO_SEQ, n = seq.length, trail = 4, head = 0;
    for (var i = 0; i < n; i++) { TriMixxx.ringLed(seq[i][0], seq[i][1], 0, 0, 0); } // clean slate
    var timerId = engine.beginTimer(55, function() {
        for (var p = 0; p < n; p++) {
            var d = head - p;                     // frames since the head passed pad p
            if (d < 0 || d > trail) { continue; } // outside the moving window
            var r = 0, g = 0, b = 0;
            if (d < trail) {                      // d == trail -> 0,0,0 clears the tail
                var bri = 255 - Math.floor(d * 255 / trail); // intro keeps its own full-range fade
                var c = TriMixxx.hueWheel(Math.floor(p * 255 / n));
                r = Math.floor(c[0] * bri / 255);
                g = Math.floor(c[1] * bri / 255);
                b = Math.floor(c[2] * bri / 255);
            }
            TriMixxx.ringLed(seq[p][0], seq[p][1], r, g, b);
        }
        head += 1;
        if (head >= n + trail) {
            engine.stopTimer(timerId);
            if (onDone) { onDone(); } // paint the real button states over the blanked ring
        }
    }, false);
};

TriMixxx.init = function(id, debugging) {
    if (TriMixxx.UNIT.deck) {
        print("TriMixxx: buttons and lights remapped for " + TriMixxx.UNIT.deck);
    }
    // Tempo fader span: the 14-bit `rate` CC is scaled by the deck's rate range,
    // which defaults to +/-8%. Widen it here so the fader covers +/-RATE_RANGE.
    engine.setValue(TriMixxx.DECK, "rateRange", TriMixxx.RATE_RANGE);
    // Quantize on by default: native loop in/out, cue and hotcues snap to the
    // beatgrid, which is what makes Mixxx's manual looping land cleanly on beats.
    engine.setValue(TriMixxx.DECK, "quantize", 1);
    // Master tempo (CDJ naming) = Mixxx's `keylock`: hold the track's pitch
    // while the tempo fader changes speed. It's a persisted control, so Mixxx
    // saves whatever state the last session ended in -- forcing it here means
    // every boot starts locked regardless of how the previous DJ left it.
    engine.setValue(TriMixxx.DECK, "keylock", 1);
    // Show elapsed AND remaining. This is forced here rather than left to
    // mixxx.cfg because WNumberPos::mousePressEvent CYCLES the mode on click
    // (elapsed -> remaining -> both) and writes it back, so on a touchscreen one
    // stray tap silently changes it for good. 2 = ELAPSED_AND_REMAINING; the
    // single control is global, which is also why one widget shows both and a
    // second one could only ever repeat it.
    engine.setValue("[Controls]", "ShowDurationRemaining", 2);

    // Mixxx's own deck sync stays off. SYNC on this deck is Pro DJ Link's
    // ([ProLink],sync_enabled), which writes the tempo itself; Mixxx's would be
    // a second thing deciding it, with no light on the screen to say so. It is
    // one key away -- "1" on any USB keyboard plugged in for maintenance -- so
    // it is put back whenever anything turns it on, not only at boot.
    engine.setValue(TriMixxx.DECK, "sync_enabled", 0);
    TriMixxx.watch(TriMixxx.DECK, "sync_enabled", function(value) {
        if (value) {
            engine.setValue(TriMixxx.DECK, "sync_enabled", 0);
        }
    });

    // Return to the waveform whenever a track is loaded (from the hardware
    // encoder push or an on-screen library tap), so the library never stays up
    // over the deck. [Master],show_library is the skin's deck/library toggle.
    //
    // Except for a track autoplay picked by itself ([Browser],autoplay_picked):
    // the DJ may be in the library looking for what comes next, and a load
    // nobody asked for must not throw them out of it. The track autoplay
    // starts from is the DJ's pick, and returns to the waveform like any other.
    TriMixxx.trackLoadedConn = engine.makeConnection(TriMixxx.DECK, "track_loaded", function(value) {
        if (value) {
            if (!engine.getValue("[Browser]", "autoplay_picked")) {
                engine.setValue("[Master]", "show_library", 0);
            }
            TriMixxx.onTrackLoaded();
        }
    });
    // The fader's owner needs to know when SYNC takes and gives back the tempo,
    // and when anything else moves it. See "Tempo fader" above.
    TriMixxx.watch("[ProLink]", "following", TriMixxx.onFollowingChanged);
    TriMixxx.watch(TriMixxx.DECK, "rate", TriMixxx.onRateChanged);
    TriMixxx.requestFaderReport();

    // Button LED indicators: connect each deck control to its colour updater.
    TriMixxx.ledConnect("rateRange", TriMixxx.ledTempoRange);
    TriMixxx.ledConnect("keylock", TriMixxx.ledKeylock);
    // B6 is two buttons, one pad: SORT over the library, KEY SYNC over the
    // deck. The light has to follow whichever it currently is, so every input
    // to either meaning repaints it -- including the view itself, which is what
    // decides which meaning is in force.
    //
    // Re-applying the sort on entry is not belt and braces: moving between
    // features rebuilds the track model and the sort goes with it, so without
    // this the light would claim a sort that is no longer in effect.
    TriMixxx.watch("[Master]", "show_library", TriMixxx.ledB6);
    TriMixxx.watch("[Browser]", "sort_column", TriMixxx.ledB6);
    TriMixxx.watch("[Browser]", "sort_order", TriMixxx.ledB6);
    TriMixxx.watch("[Browser]", "in_track_list", TriMixxx.ledB6);
    TriMixxx.watch("[ProLink]", "key_shift", TriMixxx.ledB6);
    TriMixxx.watch("[ProLink]", "key_sync_available", TriMixxx.ledB6);
    TriMixxx.ledConnect("loop_enabled", TriMixxx.ledLoopMods);
    // PLAY, CUE and the loop pads' own lights, which used to be the XML's
    // <output>s: here, so that the screen off can put them out as well.
    TriMixxx.ledConnect("play_indicator", function(v) { TriMixxx.noteLed(0x3C, v); });
    TriMixxx.ledConnect("cue_indicator", function(v) { TriMixxx.noteLed(0x3D, v); });
    TriMixxx.ledConnect("loop_enabled", function(v) {
        TriMixxx.noteLed(0x3E, v);
        TriMixxx.noteLed(0x3F, v);
    });
    // The Bluetooth receiver's screen off takes every light with it. A Mixxx
    // without the receiver has no [Bluetooth] controls, and the controls'
    // gate then never asks after them.
    TriMixxx.hasBluetooth = !!TriMixxx.watch("[Bluetooth]", "screen_off", TriMixxx.onScreenOff);
    TriMixxx.ledConnect("beatloop_8_enabled", function() { TriMixxx.ledBeatloop(8, 2); });
    TriMixxx.ledConnect("beatloop_4_enabled", function() { TriMixxx.ledBeatloop(4, 3); });
    TriMixxx.ledConnect("slip_enabled", TriMixxx.ledSlip);
    for (var h = 1; h <= 4; h++) {
        (function(idx) {
            TriMixxx.ledConnect("hotcue_" + idx + "_enabled", function() { TriMixxx.ledHotcue(idx); });
            TriMixxx.ledConnect("hotcue_" + idx + "_color", function() { TriMixxx.ledHotcue(idx); });
        }(h));
    }

    // One-shot startup rainbow-wave, then paint the real button states over it.
    TriMixxx.playIntro(TriMixxx.paintAll);
};

TriMixxx.shutdown = function() {
    if (TriMixxx.slipTimer) { engine.stopTimer(TriMixxx.slipTimer); TriMixxx.slipTimer = 0; }
    for (var i = 0; i < TriMixxx.conns.length; i++) { TriMixxx.conns[i].disconnect(); }
    if (TriMixxx.trackLoadedConn) { TriMixxx.trackLoadedConn.disconnect(); }
    // Clear every light: the rings (SysEx off), and PLAY, CUE and the loops.
    TriMixxx.lightsOff = false;
    TriMixxx.allLightsOut();
};

// ---- Tempo fader: the one thing that writes `rate` from the fader ----------
//
// The fader used to be bound to [Channel1],rate in the XML with Mixxx's
// <soft-takeover/>, which knows nothing about SYNC. While SYNC followed a deck,
// a fader moved across the followed tempo was let through, and the fader and
// SYNC then fought over the tempo thirty times a second; and soft-takeover's
// catch is a proximity of 3/128 of the travel -- 0.36 BPM at +/-6%, 6 BPM at
// WIDE -- with the tempo jumping by that much when it caught.
//
// So the script owns it, in docs/tempo-sync.md's terms:
//
//  * While SYNC follows a deck ([ProLink],following) the fader writes nothing.
//  * Whenever something other than the fader moves the tempo -- SYNC, a range
//    change, a keyboard -- the fader is "armed": it does nothing until it
//    crosses the tempo actually playing, or comes within PICKUP_BPM of it, and
//    then leads. A crossing is two consecutive positions on either side.
//  * A track loaded while not following starts at the fader's tempo, with the
//    fader live (owner decision 5).
//
// [TriMixxx],tempo_fader is published for the tempo panel in the same -1..1 as
// `rate`, from the same number the script writes to `rate`, so the panel can
// say "caught" by comparing the two rather than modelling a threshold.
TriMixxx.PICKUP_BPM = 0.05;
TriMixxx.faderMsb = 0;
TriMixxx.faderLsb = 0;
TriMixxx.faderRate = null;      // where the fader is, as a `rate`; null until heard
TriMixxx.pickupArmed = true;    // until the fader has been heard, it leads nothing
TriMixxx.lastFaderBpm = null;   // the fader's tempo at its previous message
TriMixxx.lastSetRate = null;    // the last `rate` this script wrote
TriMixxx.following = false;
TriMixxx.startAtFaderPending = false;

// The 14-bit position as a `rate`: the firmware puts the centre detent at 8192
// exactly, so that is 0, and each half of the travel spans its own 1.0. The
// script writes `rate` from this and publishes it as tempo_fader, so the two
// cannot disagree about where the fader is.
TriMixxx.faderRateOf = function(raw) {
    return raw >= 8192 ? (raw - 8192) / 8191 : (raw - 8192) / 8192;
};

// The tempo the deck would play with `rate` at *rate*: its own tempo at this
// point of the grid (bpm over rate_ratio) times the fader's ratio.
TriMixxx.tempoAtRate = function(rate) {
    var bpm = engine.getValue(TriMixxx.DECK, "bpm");
    var ratio = engine.getValue(TriMixxx.DECK, "rate_ratio");
    if (bpm <= 0 || ratio <= 0) { return null; }
    var dir = engine.getValue(TriMixxx.DECK, "rate_dir");
    var range = engine.getValue(TriMixxx.DECK, "rateRange");
    return (bpm / ratio) * (1 + dir * range * rate);
};

TriMixxx.setRate = function(rate) {
    TriMixxx.lastSetRate = rate;
    engine.setValue(TriMixxx.DECK, "rate", rate);
};

// Anything but this script moved the tempo: the fader has to catch it again.
TriMixxx.onRateChanged = function(value) {
    if (TriMixxx.lastSetRate === null || Math.abs(value - TriMixxx.lastSetRate) > 1e-9) {
        TriMixxx.pickupArmed = true;
    }
    if (TriMixxx.following) {
        TriMixxx.widenRangeFor(value);
    }
};

TriMixxx.onFollowingChanged = function(value) {
    var now = value > 0;
    if (now === TriMixxx.following) { return; }
    TriMixxx.following = now;
    if (now) {
        TriMixxx.startAtFaderPending = false;
        TriMixxx.widenRangeFor(engine.getValue(TriMixxx.DECK, "rate"));
    }
    // Leaving SYNC holds the tempo; the fader has to come back to it.
    TriMixxx.pickupArmed = true;
};

// Owner decision 9: a tempo SYNC holds that the fader cannot reach at this
// range would only ever be "caught" at the end stop, with a drop of several
// BPM. Step the range up to the smallest one that holds it. Mixxx keeps the
// tempo on a range change, so nothing is heard; A1's colour says why.
TriMixxx.widenRangeFor = function(rate) {
    if (Math.abs(rate) <= 1) { return; }
    engine.setValue(TriMixxx.DECK, "rateRange", TriMixxx.RATE_RANGES[TriMixxx.rangeHolding(0)]);
};

// The first of RATE_RANGES, from index *from* on and wrapping round, wide enough
// to express the tempo playing now. WIDE (the last) if none is: a tempo more
// than double can be followed (owner decision 17) and no range holds it.
TriMixxx.rangeHolding = function(from) {
    var deviation = Math.abs(engine.getValue(TriMixxx.DECK, "rate_ratio") - 1);
    var n = TriMixxx.RATE_RANGES.length;
    for (var i = 0; i < n; i++) {
        var idx = (from + i) % n;
        if (TriMixxx.RATE_RANGES[idx] >= deviation) { return idx; }
    }
    return n - 1;
};

TriMixxx.onTrackLoaded = function() {
    if (TriMixxx.following) { return; }   // SYNC sets this track's tempo
    if (TriMixxx.faderRate === null) {
        // Not heard since boot: the firmware only sends on a move. Applied
        // the moment it is, as long as the deck has not started by then.
        TriMixxx.startAtFaderPending = true;
        TriMixxx.requestFaderReport();
        return;
    }
    TriMixxx.startAtFader();
};

// Ask the S3 where the fader is (MidiMap.hpp SYSEX_CMD_FADER_REPORT): it is
// otherwise only sent when it moves, so after a boot Mixxx would not know until
// the DJ touched it. An older firmware ignores the request, which is the old
// behaviour: the position arrives with the first move.
TriMixxx.requestFaderReport = function() {
    midi.sendSysexMsg([0xF0, 0x7D, 0x04, 0xF7], 4);
};

TriMixxx.startAtFader = function() {
    TriMixxx.startAtFaderPending = false;
    TriMixxx.pickupArmed = false;
    TriMixxx.setRate(TriMixxx.faderRate);
};

TriMixxx.publishFader = function() {
    var raw = (TriMixxx.faderMsb << 7) | TriMixxx.faderLsb;   // 0 .. 16383
    var rate = TriMixxx.faderRateOf(raw);
    TriMixxx.faderRate = rate;
    engine.setValue("[TriMixxx]", "tempo_fader", rate);

    if (TriMixxx.startAtFaderPending && !TriMixxx.following &&
            !engine.getValue(TriMixxx.DECK, "play")) {
        TriMixxx.startAtFader();
    }

    var faderBpm = TriMixxx.tempoAtRate(rate);
    var lastBpm = TriMixxx.lastFaderBpm;
    TriMixxx.lastFaderBpm = faderBpm;
    if (TriMixxx.following) { return; }       // decoupled while SYNC follows
    if (TriMixxx.pickupArmed) {
        var playing = engine.getValue(TriMixxx.DECK, "bpm");
        if (faderBpm === null || playing <= 0) {
            // No tempo to meet (no track): nothing to protect.
            TriMixxx.pickupArmed = false;
        } else {
            var near = Math.abs(faderBpm - playing) <= TriMixxx.PICKUP_BPM;
            var crossed = lastBpm !== null && (lastBpm - playing) * (faderBpm - playing) < 0;
            if (!near && !crossed) { return; }
            TriMixxx.pickupArmed = false;
        }
    }
    TriMixxx.setRate(rate);
};

// Published on the LSB, because the firmware sends the pair MSB first and LSB
// second, every time the fader moves (firmwares/trimixxx-midi/src/main.cpp).
TriMixxx.tempoFaderMsb = function(channel, control, value, status, group) {
    TriMixxx.faderMsb = value;
};

TriMixxx.tempoFaderLsb = function(channel, control, value, status, group) {
    // The fader goes on being heard in the Bluetooth receiver, or its owner
    // would lose track of where it is; moving it still wakes the screen.
    TriMixxx.btTakes(1, TriMixxx.BT_PASSIVE);
    TriMixxx.faderLsb = value;
    TriMixxx.publishFader();
};

// ---- Ring button handlers (the loops + hotcues are direct controls in the XML;
//      these are the ones that need logic). ----

// A1 Tempo range: step the pitch-fader range through RATE_RANGES (wraps).
TriMixxx.tempoRange = function(channel, control, value, status, group) {
    if (TriMixxx.btTakes(value, TriMixxx.BT_DECK)) { return; }
    if (!value) { return; } // press only
    var cur = engine.getValue(TriMixxx.DECK, "rateRange"), idx = 0;
    for (var i = 0; i < TriMixxx.RATE_RANGES.length; i++) {
        if (Math.abs(TriMixxx.RATE_RANGES[i] - cur) < 0.001) { idx = i; break; }
    }
    // Owner decision 11: the tempo is kept and the fader catches its new
    // position. A range too narrow to express the tempo now playing would
    // leave a fader that can only catch at its end stop, so it is skipped.
    idx = TriMixxx.rangeHolding((idx + 1) % TriMixxx.RATE_RANGES.length);
    engine.setValue(TriMixxx.DECK, "rateRange", TriMixxx.RATE_RANGES[idx]);
};

// A2: master tempo (Mixxx's keylock), in every view. SORT lives on B6.
//
// With the deck shifted by KEY SYNC or the key list, toggling it keeps the
// semitone shift: KeyControl zeroes pitch_adjust on the toggle and the fork's
// KEY SYNC puts it straight back (owner decision 8).
TriMixxx.keylock = function(channel, control, value, status, group) {
    if (TriMixxx.btTakes(value, TriMixxx.BT_DECK)) { return; }
    if (!value) { return; }
    engine.setValue(TriMixxx.DECK, "keylock", !engine.getValue(TriMixxx.DECK, "keylock"));
};

// B6: SORT while the library is open, KEY SYNC over the deck.
//
// One pad, two jobs, decided by what is on screen when it goes down -- the same
// rule as A2. There is no track list to sort over the deck, and no reason to
// reach for the CDJ link while reading one. Both tell a press from a hold, so
// the release goes to whichever job took the press: the view changing under a
// held pad must not hand one half of a press to each.
TriMixxx.b6Job = null;
TriMixxx.sortKey = function(channel, control, value, status, group) {
    if (TriMixxx.btTakes(value, TriMixxx.BT_DECK)) { return; }
    if (value) {
        TriMixxx.b6Job = engine.getValue("[Master]", "show_library")
            ? TriMixxx.sortButton
            : TriMixxx.keySyncButton;
    }
    var job = TriMixxx.b6Job;
    if (!value) {
        TriMixxx.b6Job = null;
    }
    if (job) {
        job(value);
    }
};

// KEY SYNC (Sam, 2026-10-14). A press puts the deck on the master's Camelot
// number, or back in the track's own key when it is there already or there is
// nothing to match; the rules are Mixxx's (keysync.h), and the press only says
// it happened. A hold raises the key list, master or no master.
//
// As with SORT, the two can only be told apart on the release: the timer
// raises the list, and a release that beats it is the press.
TriMixxx.keySyncHoldTimer = 0;
TriMixxx.keySyncButton = function(value) {
    if (value) {
        if (TriMixxx.keyPickerOpen()) {
            // B6 again with the list up puts it away, as BACK does.
            engine.setValue("[KeyPicker]", "open", 0);
            return;
        }
        TriMixxx.keySyncHoldTimer = engine.beginTimer(TriMixxx.LONG_PRESS_MS, function() {
            TriMixxx.keySyncHoldTimer = 0;
            // Refused by the list itself with no track on the deck, or with
            // the library opened in the meantime.
            engine.setValue("[KeyPicker]", "open", 1);
        }, true);
        return;
    }
    if (!TriMixxx.keySyncHoldTimer) {
        return;  // the hold already fired, or the press closed the list
    }
    engine.stopTimer(TriMixxx.keySyncHoldTimer);
    TriMixxx.keySyncHoldTimer = 0;
    engine.setValue("[ProLink]", "key_sync", 1);
};

// Whether the key list is up. While it is, the encoder, its push, BACK and B6
// are its own.
TriMixxx.keyPickerOpen = function() {
    return engine.getValue("[KeyPicker]", "open") > 0;
};

// Press cycles the sort; hold clears it.
//
// The distinction has to wait for the release, because a long press is only
// knowable by nothing having happened yet: the timer fires the clear, and the
// release cancels it if it got there first.
TriMixxx.sortButton = function(value) {
    if (value) {
        // Nothing happens on the press. A hold has to be able to *not* cycle,
        // and the only way to know a press was short is to see it end: acting
        // immediately meant every hold cycled first and cleared afterwards.
        TriMixxx.sortHoldTimer = engine.beginTimer(TriMixxx.LONG_PRESS_MS, function() {
            TriMixxx.sortHoldTimer = 0;
            // Held: the other track-list layout (browser-prd.md 8.2).
            engine.setValue("[Browser]", "info_toggle", 1);
        }, true);
        return;
    }
    if (!TriMixxx.sortHoldTimer) {
        return;  // the hold already fired; the release is just the end of it
    }
    engine.stopTimer(TriMixxx.sortHoldTimer);
    TriMixxx.sortHoldTimer = 0;
    // Short: raise the sort menu. The browser ignores it unless a track list is
    // on screen, so this needs no condition of its own.
    engine.setValue("[Browser]", "sort_menu", 1);
};

// B5 Slip mode: toggle on press.
TriMixxx.slip = function(channel, control, value, status, group) {
    if (TriMixxx.btTakes(value, TriMixxx.BT_DECK)) { return; }
    if (!value) { return; }
    engine.setValue(TriMixxx.DECK, "slip_enabled", !engine.getValue(TriMixxx.DECK, "slip_enabled"));
};

// A7 Back: reverse of the browse-encoder push. Library hidden -> open it on the
// sidebar; on the track list -> step back to the left sidebar columns; already on
// the sidebar -> close the library back to the deck.
TriMixxx.backHoldTimer = 0;
// When the last BACK press was taken seriously, for BACK_DEBOUNCE_MS.
TriMixxx.backLastAt = 0;

// A7 BACK. Short: up one level. Long: back to the deck, keeping your place.
//
// The long press is the one a DJ actually reaches for mid-set -- you are four
// levels into a stick, the track is running out, and you want the waveform
// without losing where you were. Nothing tells the browser to unwind, so the
// menu stack survives and re-opening lands exactly where you left.
//
// As with SORT, the distinction can only be made on the RELEASE: a long press
// is knowable only by nothing having happened yet.
TriMixxx.back = function(channel, control, value, status, group) {
    if (TriMixxx.btTakes(value, TriMixxx.BT_NAV)) { return; }
    if (value) {
        // The debounce goes on the PRESS, not on the action, because the press
        // is what arms the hold timer -- and it was the second timer, armed by
        // a bounce and stopped by the bounce's own release, that popped a
        // second level. Reject the press and the release that follows finds
        // nothing to fire, so one physical press means one level either way.
        var now = Date.now();
        if (now - TriMixxx.backLastAt < TriMixxx.BACK_DEBOUNCE_MS) {
            return;
        }
        TriMixxx.backLastAt = now;
        if (TriMixxx.keyPickerOpen()) {
            // The key list goes, changing nothing, and that is all: the deck
            // under it stays on screen. On the press, with nothing to wait for.
            engine.setValue("[KeyPicker]", "open", 0);
            return;
        }
        if (!engine.getValue("[Master]", "show_library")) {
            // Opening is unambiguous -- there is no second meaning to wait for,
            // and making the DJ hold the button to see the library would be
            // absurd. Acts on the press.
            engine.setValue("[Master]", "show_library", 1);
            return;
        }
        TriMixxx.backHoldTimer = engine.beginTimer(TriMixxx.LONG_PRESS_MS, function() {
            TriMixxx.backHoldTimer = 0;
            engine.setValue("[Master]", "show_library", 0);
        }, true);
        return;
    }
    if (!TriMixxx.backHoldTimer) {
        return;  // The hold already fired; this release is just the end of it.
    }
    engine.stopTimer(TriMixxx.backHoldTimer);
    TriMixxx.backHoldTimer = 0;
    engine.setValue("[Browser]", "back", 1);
};

// ==== Ring button LED indicators (coloured, via SysEx cmd 0x01=A / 0x03=B) ====

// Push an already-computed [r,g,b] to a node (both LEDs).
TriMixxx.send = function(ring, node, rgb) { TriMixxx.ringLed(ring, node, rgb[0], rgb[1], rgb[2]); };

// Brightness helpers. Both take an optional bri (0..1) that overrides the global
// BRIGHTNESS for that one call, so brightness is settable case by case.
//   dim(c)   -- NORMALIZE to full intensity (max channel -> 255) then scale by
//               bri. For palette hues (only the ratio matters), so every
//               indicator lands at the same brightness. [0,0,0] stays off.
//   bound(c) -- CAP so the brightest channel is <= bri*255, else untouched. For
//               real colours (Mixxx hotcues): keep the colour's own intensity,
//               just don't let it exceed the ceiling.
TriMixxx.dim = function(c, bri) {
    if (bri === undefined) { bri = TriMixxx.BRIGHTNESS; }
    var m = Math.max(c[0], c[1], c[2]);
    var k = (m > 0 ? 255 / m : 0) * bri;
    return [Math.round(c[0] * k), Math.round(c[1] * k), Math.round(c[2] * k)];
};
TriMixxx.bound = function(c, bri) {
    if (bri === undefined) { bri = TriMixxx.BRIGHTNESS; }
    var ceil = 255 * bri, m = Math.max(c[0], c[1], c[2]);
    if (m <= ceil) { return [c[0], c[1], c[2]]; }
    var k = ceil / m;
    return [Math.round(c[0] * k), Math.round(c[1] * k), Math.round(c[2] * k)];
};

// Palette indicator -> node (both LEDs), normalized via dim(); optional per-call bri.
TriMixxx.led = function(ring, node, c, bri) { TriMixxx.send(ring, node, TriMixxx.dim(c, bri)); };

// A1 tempo range: colour by the current pitch range (green/yellow/orange/red).
TriMixxx.ledTempoRange = function() {
    var cur = engine.getValue(TriMixxx.DECK, "rateRange"), c = TriMixxx.C_RED;
    for (var i = 0; i < TriMixxx.RANGE_LED.length; i++) {
        if (Math.abs(TriMixxx.RANGE_LED[i][0] - cur) < 0.001) { c = TriMixxx.RANGE_LED[i][1]; break; }
    }
    TriMixxx.led(0x01, 0, c);
};

// A2 master tempo: off when keylock off, red when on.
TriMixxx.ledKeylock = function() {
    TriMixxx.led(0x01, 1, engine.getValue(TriMixxx.DECK, "keylock") ? TriMixxx.C_RED : TriMixxx.C_OFF);
};

// B6: whichever of its two meanings is in force. See TriMixxx.sortKey.
TriMixxx.ledB6 = function() {
    if (engine.getValue("[Master]", "show_library")) {
        TriMixxx.ledSort();
    } else {
        TriMixxx.ledKeySync();
    }
};

// KEY SYNC pad: bright while the deck is out of the track's own key (KEY SYNC
// or the key list moved it), dim when a press would put it on the master's
// key -- a CDJ has master, and both keys are known -- and dark otherwise.
//
// Bright wins over dark deliberately: a deck shifted onto a master that has
// since gone is still shifted, and the pad has to keep saying so -- it is the
// only thing on the deck that does, and it is still the button that goes back.
TriMixxx.ledKeySync = function() {
    if (engine.getValue("[ProLink]", "key_shift")) {
        TriMixxx.led(0x03, 5, TriMixxx.C_KEY_SYNC, TriMixxx.SORT_BRIGHT);
        return;
    }
    if (!engine.getValue("[ProLink]", "key_sync_available")) {
        TriMixxx.led(0x03, 5, TriMixxx.C_OFF);
        return;
    }
    // Armed: one LED of the two, at KEY_SYNC_DIM. dim() normalizes the hue
    // first, so the half that is lit is the same purple as the engaged state
    // and only its intensity differs.
    TriMixxx.ringLedPair(0x03, 5,
        TriMixxx.dim(TriMixxx.C_KEY_SYNC, TriMixxx.KEY_SYNC_DIM),
        TriMixxx.C_OFF);
};

// SORT pad: the sort field's colour, dim ascending and bright descending, and
// dark whenever there is no track list for the button to act on.
TriMixxx.ledSort = function() {
    if (!engine.getValue("[Browser]", "in_track_list")) {
        TriMixxx.led(0x03, 5, TriMixxx.C_OFF);
        return;
    }
    var index = Math.round(engine.getValue("[Browser]", "sort_column"));
    var colour = (index >= 0 && index < TriMixxx.SORT_COLOURS.length)
        ? TriMixxx.SORT_COLOURS[index]
        : null;
    if (!colour) {
        // Default: sorted by nothing, so the pad says nothing.
        TriMixxx.led(0x03, 5, TriMixxx.C_OFF);
        return;
    }
    var descending = engine.getValue("[Browser]", "sort_order") > 0;
    TriMixxx.led(0x03, 5, colour,
        descending ? TriMixxx.SORT_BRIGHT : TriMixxx.SORT_DIM);
};

// A5/A6 double/halve: orange when there is a loop to act on, else off.
TriMixxx.ledLoopMods = function() {
    var c = engine.getValue(TriMixxx.DECK, "loop_enabled") ? TriMixxx.C_ORANGE : TriMixxx.C_OFF;
    TriMixxx.led(0x01, 4, c);
    TriMixxx.led(0x01, 5, c);
};

// A3/A4 8/4-beat loop: orange while that loop size is running.
TriMixxx.ledBeatloop = function(beats, node) {
    var on = engine.getValue(TriMixxx.DECK, "beatloop_" + beats + "_enabled");
    TriMixxx.led(0x01, node, on ? TriMixxx.C_ORANGE : TriMixxx.C_OFF);
};

// B5 slip: solid red when off, flashing bright red while slip is on.
TriMixxx.slipFlash = function() {
    TriMixxx.slipPhase = !TriMixxx.slipPhase;
    TriMixxx.led(0x03, 4, TriMixxx.slipPhase ? TriMixxx.C_RED_HI : TriMixxx.C_OFF);
};
TriMixxx.ledSlip = function() {
    if (engine.getValue(TriMixxx.DECK, "slip_enabled")) {
        if (!TriMixxx.slipTimer) {
            TriMixxx.slipTimer = engine.beginTimer(350, TriMixxx.slipFlash, false);
            TriMixxx.slipFlash(); // light immediately, don't wait for the first tick
        }
    } else if (TriMixxx.slipTimer) {
        engine.stopTimer(TriMixxx.slipTimer);
        TriMixxx.slipTimer = 0;
        TriMixxx.led(0x03, 4, TriMixxx.C_RED);
    } else {
        TriMixxx.led(0x03, 4, TriMixxx.C_RED); // initial paint (off + not yet flashing)
    }
};

// B1..B4 hotcues: the hotcue's own Mixxx colour when set (bounded to BRIGHTNESS,
// NOT normalized -- a dim hotcue colour stays dim), off when empty.
TriMixxx.ledHotcue = function(idx) {
    var rgb = [0, 0, 0];
    if (engine.getValue(TriMixxx.DECK, "hotcue_" + idx + "_enabled")) {
        var col = engine.getValue(TriMixxx.DECK, "hotcue_" + idx + "_color"); // packed 0xRRGGBB
        rgb = [(col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF];
    }
    TriMixxx.send(0x03, idx - 1, TriMixxx.bound(rgb));
};

// Register a control -> LED updater (tracked so shutdown can disconnect).
//
// A connection that could not be made is NOT stored. makeConnection returns
// undefined for a control that does not exist, and one undefined in this list
// threw out of paintAll -- which Mixxx reports as "the mapping is not working
// properly" and then stops running the script. One missing LED is worth one
// missing LED; it is not worth every button on the deck going dead mid-set.
//
// [ProLink] controls are the reason this takes a group at all: they are created
// by Mixxx's core services, which run before any script, so they are there --
// but a build without Pro DJ Link has none of them, and that must cost the KEY
// SYNC light and nothing else.
TriMixxx.watch = function(group, control, cb) {
    var conn = engine.makeConnection(group, control, cb);
    if (conn) {
        TriMixxx.conns.push(conn);
    } else {
        print("TriMixxx: no such control, LED not connected: " + group + "," + control);
    }
    return conn;
};

TriMixxx.ledConnect = function(control, cb) {
    TriMixxx.watch(TriMixxx.DECK, control, cb);
};

// Paint every button to its current state: trigger all connections, then the two
// static buttons. Called once after the intro, and re-usable any time.
TriMixxx.paintAll = function() {
    // Guarded as well as filtered at insert: this runs from a timer callback,
    // and an exception here is one the script cannot recover from.
    for (var i = 0; i < TriMixxx.conns.length; i++) {
        if (TriMixxx.conns[i]) { TriMixxx.conns[i].trigger(); }
    }
    TriMixxx.led(0x01, 6, TriMixxx.C_DIM_W); // A7 back: dim white (static)
    // B6 explicitly as well as through its connections: without Pro DJ Link
    // those never connected, and an unpainted pad keeps whatever the intro
    // animation left on it.
    TriMixxx.ledB6();
};

// ---- the Bluetooth receiver ([Bluetooth]) -----------------------------------
// While the deck is a Bluetooth receiver ([Bluetooth],active: its menu is open)
// it plays a phone, and Mixxx has let go of the sound card: the deck's own
// controls do nothing to it (the [for Sam] call in the receiver's PR). The
// encoder and BACK still move through the menus, and the tempo fader is still
// followed. With the screen off ([Bluetooth],screen_off) every light on the S3
// is out too, and any control wakes both and does nothing else (Sam: "back on
// whenever a control is touched"). Every control also says it was touched
// ([Bluetooth],wake), which keeps the screen on three minutes more.
//
// A release always goes through: it only ever ends what a press began, and a
// press taken here began nothing.
TriMixxx.BT_NAV = 1;      // the encoder and BACK: the menus
TriMixxx.BT_PASSIVE = 2;  // heard whatever happens: the tempo fader
TriMixxx.BT_DECK = 3;     // everything that would play or change the deck
TriMixxx.lightsOff = false;
TriMixxx.hasBluetooth = false;
TriMixxx.btTakes = function(value, kind) {
    if (!value || !TriMixxx.hasBluetooth || !(engine.getValue("[Bluetooth]", "active") > 0)) {
        return false;
    }
    if (engine.getValue("[Bluetooth]", "screen_off") > 0) {
        engine.setValue("[Bluetooth]", "screen_off", 0);
        return kind !== TriMixxx.BT_PASSIVE;
    }
    engine.setValue("[Bluetooth]", "wake", 1);
    return kind === TriMixxx.BT_DECK;
};

// PLAY, CUE and the loop pads light by Note-On velocity, at the note their LED
// answers to on this deck (`lights`; the XML's <output>s were remapped the same
// way by units/apply.py).
TriMixxx.noteLed = function(note, value) {
    if (TriMixxx.lightsOff) { return; }
    var physical = TriMixxx.UNIT.lights[note];
    midi.sendShortMsg(0x90, physical === undefined ? note : physical, value >= 0.5 ? 0x7F : 0x00);
};
TriMixxx.NOTE_LEDS = [0x3C, 0x3D, 0x3E, 0x3F];

// Every light out, now.
TriMixxx.allLightsOut = function() {
    for (var a = 0; a < TriMixxx.RING_A_N; a++) { TriMixxx.ringLed(0x01, a, 0, 0, 0); }
    for (var b = 0; b < TriMixxx.RING_B_N; b++) { TriMixxx.ringLed(0x03, b, 0, 0, 0); }
    for (var n = 0; n < TriMixxx.NOTE_LEDS.length; n++) { TriMixxx.noteLed(TriMixxx.NOTE_LEDS[n], 0); }
};

// The screen off: the lights go with it, and nothing lights them until it is
// on again, when they are all painted as they stand.
TriMixxx.onScreenOff = function(value) {
    if (value > 0) {
        TriMixxx.lightsOff = false;
        TriMixxx.allLightsOut();
        TriMixxx.lightsOff = true;
    } else if (TriMixxx.lightsOff) {
        TriMixxx.lightsOff = false;
        TriMixxx.paintAll();
    }
};

// ---- the deck's own buttons that set one control each -------------------
// CUE, LOOP IN/OUT, RELOOP and ring A's loop pads, by canonical note: the
// momentary ones follow the button (1 down, 0 up), the others act on the
// press. Bound in the XML to this rather than to their controls directly, so
// that the Bluetooth receiver can stop them.
TriMixxx.BUTTONS = {
    0x3D: ["cue_default", true],
    0x3E: ["loop_in", true],      // released, a loop point stops being dragged
    0x3F: ["loop_out", true],
    0x40: ["reloop_toggle", false],
    0x02: ["beatloop_8_toggle", false],
    0x03: ["beatloop_4_toggle", false],
    0x04: ["loop_double", false],
    0x05: ["loop_halve", false]
};
TriMixxx.button = function(channel, control, value, status, group) {
    var press = (status & 0xF0) === 0x90 && value > 0;
    if (TriMixxx.btTakes(press ? 1 : 0, TriMixxx.BT_DECK)) { return; }
    var button = TriMixxx.BUTTONS[TriMixxx.canonicalNote(control)];
    if (!button) { return; }
    if (button[1]) {
        engine.setValue(group, button[0], press ? 1 : 0);
    } else if (press) {
        engine.setValue(group, button[0], 1);
    }
};

// ---- Play/pause: toggle on press ----
TriMixxx.play = function(channel, control, value, status, group) {
    if (TriMixxx.btTakes(value, TriMixxx.BT_DECK)) { return; }
    if (value) {
        engine.setValue(group, "play", !engine.getValue(group, "play"));
    }
};

// ---- Track browse encoder: firmware sends 1 = up, 127 = down (one per detent). ----
TriMixxx.browse = function(channel, control, value, status, group) {
    if (TriMixxx.btTakes(value, TriMixxx.BT_NAV)) { return; }
    if (TriMixxx.keyPickerOpen()) {
        // The key list, while it is up, the way the library goes: up is up
        // the list, towards +6.
        engine.setValue("[KeyPicker]", "move", (value === 1) ? -1 : 1);
    } else if (engine.getValue("[Master]", "show_library")) {
        // Browsing: one encoder, one selection. There is no pane to pick any
        // more -- the browser has a single focus (browser-prd.md 4.3).
        engine.setValue("[Browser]", "move", (value === 1) ? -1 : 1);
    } else {
        // Playing view: zoom the waveform. (up = zoom in; swap the two control
        // names if the direction feels inverted.)
        engine.setValue(TriMixxx.DECK, (value === 1) ? "waveform_zoom_down" : "waveform_zoom_up", 1);
    }
};

// Hotcues: activate (jump if set, create at the playhead if empty), both edges so
// a press-hold previews while paused. When WE create one (empty -> set), colour it
// from the sequence below; a loaded track's own hotcues keep their stored colours
// because that path never runs this handler.
TriMixxx.HOTCUE_COLORS = [0xFE0000, 0xFDFE02, 0x0BFF01, 0x011EFE, 0xFE00F6];
TriMixxx.hotcue = function(channel, control, value, status, group) {
    if (TriMixxx.btTakes(value, TriMixxx.BT_DECK)) { return; }
    var idx = TriMixxx.canonicalNote(control) - 0x42; // note 0x43 -> hotcue 1
    if (!value) {
        engine.setValue(group, "hotcue_" + idx + "_activate", 0); // end any preview
        return;
    }
    var wasEmpty = !engine.getValue(group, "hotcue_" + idx + "_enabled");
    engine.setValue(group, "hotcue_" + idx + "_activate", 1);
    if (wasEmpty && idx <= TriMixxx.HOTCUE_COLORS.length) {
        engine.setValue(group, "hotcue_" + idx + "_color", TriMixxx.HOTCUE_COLORS[idx - 1]);
    }
};

// ---- Track encoder push -------------------------------------------------
// Deck view: open the browser. Browsing: activate whatever is selected, and
// what that means -- enter a medium, open a category, load a track -- is the
// browser's to decide. There used to be a focus dance here, reading and writing
// [Library],focused_widget to tell a sidebar from a track table; the browser
// has one focus and one selection, so there is nothing left to disambiguate.
TriMixxx.encoderPush = function(channel, control, value, status, group) {
    if (TriMixxx.btTakes(value, TriMixxx.BT_NAV)) { return; }
    if (!value) { return; } // press only

    if (TriMixxx.keyPickerOpen()) {
        // The key list: pick the selected key.
        engine.setValue("[KeyPicker]", "select", 1);
        return;
    }
    if (!engine.getValue("[Master]", "show_library")) {
        engine.setValue("[Master]", "show_library", 1);
        return;
    }
    // One control for every level: the browser knows whether the selection is
    // a source, a category, a playlist or a track, and what each means.
    engine.setValue("[Browser]", "select", 1);
};

// ---- Jog touch: enable scratch while held, pitch-bend when released ----
TriMixxx.jogTouch = function(channel, control, value, status, group) {
    if (TriMixxx.btTakes(value, TriMixxx.BT_DECK)) { return; }
    if (value) {
        // ticks/rev, 33.3 rpm vinyl. Gearing goes through ticks/rev rather than
        // through the tick delta: scratchTick() takes an *int*, so a scaled
        // delta would truncate to 0 on slow turns and drop fine movement
        // entirely. Mixxx derives m_dx = 60 / (rpm * intervalsPerRev), so a
        // larger ticks/rev means less track movement per tick.
        engine.scratchEnable(
            TriMixxx.DECK_NUM,
            Math.round(TriMixxx.JOG_TICKS_REV / TriMixxx.JOG_SCRATCH_RATIO),
            33 + 1 / 3, TriMixxx.JOG_ALPHA, TriMixxx.JOG_BETA);
        TriMixxx.scratching = true;
    } else {
        // ramp=false: the default (true) ramps the deck back to its playback
        // rate on release, i.e. a spinback-style coast. Cutting straight to the
        // deck rate is what makes the release feel stable rather than loose.
        engine.scratchDisable(TriMixxx.DECK_NUM, false);
        TriMixxx.scratching = false;
    }
};

// ---- Jog rotate: CC value is a 7-bit two's-complement tick delta.
//      The firmware counts the opposite way to Mixxx's scratch/bend sense, so
//      negate here -- this flips scratch and pitch-bend together. ----
TriMixxx.jog = function(channel, control, value, status, group) {
    if (TriMixxx.btTakes(value, TriMixxx.BT_DECK)) { return; }
    var delta = -((value < 64) ? value : value - 128);
    if (TriMixxx.UNIT.jogReversed) {
        delta = -delta; // this deck's encoder is wired the other way round
    }
    if (TriMixxx.scratching) {
        // Raw delta: scratch sensitivity is already baked into ticks/rev above.
        engine.scratchTick(TriMixxx.DECK_NUM, delta);
    } else {
        // `jog` takes a double, so the bend path scales the delta directly.
        engine.setValue(group, "jog", delta * TriMixxx.JOG_BEND_SENSITIVITY);
    }
};
