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
TriMixxx.RATE_RANGE    = 0.16;  // tempo fader span at boot = +/-16%
TriMixxx.RATE_RANGES   = [0.06, 0.10, 0.16, 1.0]; // A1 cycles these: +/-6, 10, 16, Wide (100%)

// ---- Ring button LED palette. Entries only need correct hue RATIOS -- dim()
//      normalizes each to full intensity, then scales by BRIGHTNESS. ----
TriMixxx.BRIGHTNESS = 0.8;          // 0..1 default for indicator LEDs (per-call override below)
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
TriMixxx.ringLed = function(cmd, node, r, g, b) {
    midi.sendSysexMsg(
        [0xF0, 0x7D, cmd, node,
            (r >> 4) & 0xF, r & 0xF, (g >> 4) & 0xF, g & 0xF, (b >> 4) & 0xF, b & 0xF, 0xF7], 11);
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

    // Return to the waveform whenever a track is loaded (from the hardware
    // encoder push or an on-screen library tap), so the library never stays up
    // over the deck. [Master],show_library is the skin's deck/library toggle.
    TriMixxx.trackLoadedConn = engine.makeConnection(TriMixxx.DECK, "track_loaded", function(value) {
        if (value) {
            engine.setValue("[Master]", "show_library", 0);
        }
    });

    // Button LED indicators: connect each deck control to its colour updater.
    TriMixxx.ledConnect("rateRange", TriMixxx.ledTempoRange);
    TriMixxx.ledConnect("keylock", TriMixxx.ledKeylock);
    TriMixxx.ledConnect("loop_enabled", TriMixxx.ledLoopMods);
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
    // Clear every ring button LED (SysEx off).
    for (var a = 0; a < TriMixxx.RING_A_N; a++) { TriMixxx.ringLed(0x01, a, 0, 0, 0); }
    for (var b = 0; b < TriMixxx.RING_B_N; b++) { TriMixxx.ringLed(0x03, b, 0, 0, 0); }
};

// ---- Ring button handlers (the loops + hotcues are direct controls in the XML;
//      these are the ones that need logic). ----

// A1 Tempo range: step the pitch-fader range through RATE_RANGES (wraps).
TriMixxx.tempoRange = function(channel, control, value, status, group) {
    if (!value) { return; } // press only
    var cur = engine.getValue(TriMixxx.DECK, "rateRange"), idx = 0;
    for (var i = 0; i < TriMixxx.RATE_RANGES.length; i++) {
        if (Math.abs(TriMixxx.RATE_RANGES[i] - cur) < 0.001) { idx = i; break; }
    }
    idx = (idx + 1) % TriMixxx.RATE_RANGES.length;
    engine.setValue(TriMixxx.DECK, "rateRange", TriMixxx.RATE_RANGES[idx]);
};

// A2 Master tempo (= Mixxx keylock): toggle on press.
TriMixxx.keylock = function(channel, control, value, status, group) {
    if (!value) { return; }
    engine.setValue(TriMixxx.DECK, "keylock", !engine.getValue(TriMixxx.DECK, "keylock"));
};

// B5 Slip mode: toggle on press.
TriMixxx.slip = function(channel, control, value, status, group) {
    if (!value) { return; }
    engine.setValue(TriMixxx.DECK, "slip_enabled", !engine.getValue(TriMixxx.DECK, "slip_enabled"));
};

// A7 Back: reverse of the browse-encoder push. Library hidden -> open it on the
// sidebar; on the track list -> step back to the left sidebar columns; already on
// the sidebar -> close the library back to the deck.
TriMixxx.back = function(channel, control, value, status, group) {
    if (!value) { return; } // press only
    if (!engine.getValue("[Master]", "show_library")) {
        engine.setValue("[Master]", "show_library", 1);
        engine.setValue("[Library]", "focused_widget", TriMixxx.FOCUS_SIDEBAR);
    } else if (engine.getValue("[Library]", "focused_widget") === TriMixxx.FOCUS_TRACKS) {
        engine.setValue("[Library]", "focused_widget", TriMixxx.FOCUS_SIDEBAR);
    } else {
        engine.setValue("[Master]", "show_library", 0);
    }
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

// Register a deck-control -> LED updater (tracked so shutdown can disconnect).
TriMixxx.ledConnect = function(control, cb) {
    TriMixxx.conns.push(engine.makeConnection(TriMixxx.DECK, control, cb));
};

// Paint every button to its current state: trigger all connections, then the two
// static buttons. Called once after the intro, and re-usable any time.
TriMixxx.paintAll = function() {
    for (var i = 0; i < TriMixxx.conns.length; i++) { TriMixxx.conns[i].trigger(); }
    TriMixxx.led(0x01, 6, TriMixxx.C_DIM_W); // A7 back: dim white (static)
    TriMixxx.led(0x03, 5, TriMixxx.C_OFF);   // B6 key sync: off until the LAN link drives it
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
    if (engine.getValue("[Master]", "show_library")) {
        // Library open: scroll whichever pane has focus.
        engine.setValue("[Library]", "MoveVertical", (value === 1) ? -1 : 1);
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
    var idx = control - 0x42; // note 0x43 -> hotcue 1
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

// ---- Track encoder push: one button, three jobs, depending on where you are.
//        deck view -> open the library, focused on the sidebar
//        sidebar   -> expand the selected menu entry if it has children,
//                     otherwise step right into the track list
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
        // GoToItem, not a straight focus jump: LibraryControl::slotGoToItem does
        // exactly the branch we want. A menu entry with children (Rekordbox and
        // its USB drives, Players and the CDJs on the network, Playlists) toggles
        // expanded and KEEPS focus in the sidebar, so the next turn of the
        // encoder walks into the children. A leaf -- or a root that owns a track
        // table, i.e. Tracks -- hands focus to the track list instead, which is
        // what the old unconditional jump did for everything.
        engine.setValue("[Library]", "GoToItem", 1);
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
