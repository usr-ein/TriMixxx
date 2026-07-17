// ===========================================================================
//  TriMixxx.scripts.js  -  Mixxx controller script for the TriMixxx S3 deck.
//
//  Pairs with TriMixxx.midi.xml and mirrors the firmware's MIDI contract in
//  lib/PiLink/MidiMap.hpp EXACTLY. One deck = MIDI channel 1 = [Channel1].
//  If you change a MIDI address in MidiMap.hpp, change it here AND in the .xml.
// ===========================================================================
var TriMixxx = {};

// ---- constants (mirror MidiMap.hpp) ----
TriMixxx.NOTE_ON       = 0x90;  // note-on, MIDI channel 1
TriMixxx.PAD_BASE      = 0x00;  // ring pad note = PAD_BASE + i, i = 0..49
TriMixxx.PADS          = 50;    // PAD_COUNT
TriMixxx.DECK          = "[Channel1]";
TriMixxx.DECK_NUM      = 1;      // [Channel1] = deck 1 (single fixed deck)
TriMixxx.JOG_TICKS_REV = 12960; // JogWheel::TICKS_PER_REV (full-quad ticks / rev)
TriMixxx.RATE_RANGE    = 0.16;  // tempo fader span = +/-16% (raise for wider pitch)
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
TriMixxx.ringLast    = -1;      // last pad lit by the position indicator

TriMixxx.init = function(id, debugging) {
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
    // Ring = play-position indicator: one lit pad follows playback.
    TriMixxx.ringConn = engine.makeConnection(TriMixxx.DECK, "playposition", TriMixxx.ringUpdate);
    TriMixxx.ringConn.trigger();

    // Return to the waveform whenever a track is loaded (from the hardware
    // encoder push or an on-screen library tap), so the library never stays up
    // over the deck. [Master],show_library is the skin's deck/library toggle.
    TriMixxx.trackLoadedConn = engine.makeConnection(TriMixxx.DECK, "track_loaded", function(value) {
        if (value) {
            engine.setValue("[Master]", "show_library", 0);
        }
    });
};

TriMixxx.shutdown = function() {
    if (TriMixxx.ringConn)        { TriMixxx.ringConn.disconnect(); }
    if (TriMixxx.trackLoadedConn) { TriMixxx.trackLoadedConn.disconnect(); }
    for (var i = 0; i < TriMixxx.PADS; i++) {
        midi.sendShortMsg(TriMixxx.NOTE_ON, TriMixxx.PAD_BASE + i, 0x00); // clear the ring
    }
};

// ---- Ring: light the pad at the current play position (velocity = brightness).
//      Only emits MIDI when the lit pad changes, so <= PADS messages per pass. ----
TriMixxx.ringUpdate = function(value, group, control) {
    var pad = Math.floor(value * TriMixxx.PADS);
    if (pad < 0) { pad = 0; }
    if (pad >= TriMixxx.PADS) { pad = TriMixxx.PADS - 1; }
    if (pad === TriMixxx.ringLast) { return; }
    if (TriMixxx.ringLast >= 0) {
        midi.sendShortMsg(TriMixxx.NOTE_ON, TriMixxx.PAD_BASE + TriMixxx.ringLast, 0x00);
    }
    midi.sendShortMsg(TriMixxx.NOTE_ON, TriMixxx.PAD_BASE + pad, 0x7F);
    TriMixxx.ringLast = pad;
};

// ---- Ring pad press -> needle-drop seek. Not wired by default (the ring is a
//      position indicator); enable by adding the 50 note-on entries in the .xml
//      that point here. Pad i seeks to i/(PADS-1) of the track. ----
TriMixxx.padPress = function(channel, control, value, status, group) {
    if (value === 0) { return; } // press only
    var pad = control - TriMixxx.PAD_BASE;
    engine.setValue(TriMixxx.DECK, "playposition", pad / (TriMixxx.PADS - 1));
};

// ---- Play/pause: toggle on press ----
TriMixxx.play = function(channel, control, value, status, group) {
    if (value) {
        engine.setValue(group, "play", !engine.getValue(group, "play"));
    }
};

// ---- Track browse encoder: firmware sends 1 = up, 127 = down (one per detent).
//      MoveVertical acts on whichever library widget currently has focus, which
//      is what lets one encoder scroll the sidebar and then the track list. ----
TriMixxx.browse = function(channel, control, value, status, group) {
    engine.setValue("[Library]", "MoveVertical", (value === 1) ? -1 : 1);
};

// ---- Track encoder push: one button, three jobs, depending on where you are.
//        deck view -> open the library, focused on the sidebar
//        sidebar   -> step right into the track list
//        track list-> load the selected track (the track_loaded connection in
//                     init() then drops us back on the waveform)
//
//      Focus is both read and written through [Library],focused_widget, whose
//      values are Mixxx's FocusWidget enum (library_decl.h). Setting it calls
//      LibraryControl::setLibraryFocus(), i.e. it really does move focus. ----
TriMixxx.FOCUS_SIDEBAR = 2; // FocusWidget::Sidebar
TriMixxx.FOCUS_TRACKS  = 3; // FocusWidget::TracksTable

TriMixxx.encoderPush = function(channel, control, value, status, group) {
    if (!value) { return; } // press only

    if (!engine.getValue("[Master]", "show_library")) {
        engine.setValue("[Master]", "show_library", 1);
        // Focus the sidebar explicitly: otherwise the encoder's first turn goes
        // to whatever Mixxx happened to focus last.
        engine.setValue("[Library]", "focused_widget", TriMixxx.FOCUS_SIDEBAR);
        return;
    }

    if (engine.getValue("[Library]", "focused_widget") === TriMixxx.FOCUS_SIDEBAR) {
        engine.setValue("[Library]", "focused_widget", TriMixxx.FOCUS_TRACKS);
        return;
    }

    engine.setValue(TriMixxx.DECK, "LoadSelectedTrack", 1);
};

// ---- Jog touch: enable scratch while held, pitch-bend when released ----
TriMixxx.jogTouch = function(channel, control, value, status, group) {
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
    var delta = -((value < 64) ? value : value - 128);
    if (TriMixxx.scratching) {
        // Raw delta: scratch sensitivity is already baked into ticks/rev above.
        engine.scratchTick(TriMixxx.DECK_NUM, delta);
    } else {
        // `jog` takes a double, so the bend path scales the delta directly.
        engine.setValue(group, "jog", delta * TriMixxx.JOG_BEND_SENSITIVITY);
    }
};
