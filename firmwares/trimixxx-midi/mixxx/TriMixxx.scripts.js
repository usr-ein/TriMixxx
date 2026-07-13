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
TriMixxx.JOG_TICKS_REV = 12960; // JogWheel::TICKS_PER_REV (full-quad ticks / rev)

TriMixxx.scratching = false;
TriMixxx.ringLast    = -1;      // last pad lit by the position indicator

TriMixxx.init = function (id, debugging) {
    // Ring = play-position indicator: one lit pad follows playback.
    TriMixxx.ringConn = engine.makeConnection(TriMixxx.DECK, "playposition", TriMixxx.ringUpdate);
    TriMixxx.ringConn.trigger();
};

TriMixxx.shutdown = function () {
    for (var i = 0; i < TriMixxx.PADS; i++) {
        midi.sendShortMsg(TriMixxx.NOTE_ON, TriMixxx.PAD_BASE + i, 0x00); // clear the ring
    }
};

// ---- Ring: light the pad at the current play position (velocity = brightness).
//      Only emits MIDI when the lit pad changes, so <= PADS messages per pass. ----
TriMixxx.ringUpdate = function (value, group, control) {
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
TriMixxx.padPress = function (channel, control, value, status, group) {
    if (value === 0) { return; } // press only
    var pad = control - TriMixxx.PAD_BASE;
    engine.setValue(TriMixxx.DECK, "playposition", pad / (TriMixxx.PADS - 1));
};

// ---- Play/pause: toggle on press ----
TriMixxx.play = function (channel, control, value, status, group) {
    if (value) {
        engine.setValue(group, "play", !engine.getValue(group, "play"));
    }
};

// ---- Track browse encoder: firmware sends 1 = up, 127 = down (one per detent) ----
TriMixxx.browse = function (channel, control, value, status, group) {
    engine.setValue("[Library]", "MoveVertical", (value === 1) ? -1 : 1);
};

// ---- Jog touch: enable scratch while held, pitch-bend when released ----
TriMixxx.jogTouch = function (channel, control, value, status, group) {
    var deck = script.deckFromGroup(group);
    if (value) {
        // ticks/rev, 33.3 rpm vinyl, standard alpha/beta filter.
        engine.scratchEnable(deck, TriMixxx.JOG_TICKS_REV, 33 + 1 / 3, 1.0 / 8, (1.0 / 8) / 32);
        TriMixxx.scratching = true;
    } else {
        engine.scratchDisable(deck);
        TriMixxx.scratching = false;
    }
};

// ---- Jog rotate: CC value is a 7-bit two's-complement tick delta ----
TriMixxx.jog = function (channel, control, value, status, group) {
    var delta = (value < 64) ? value : value - 128;
    if (TriMixxx.scratching) {
        engine.scratchTick(script.deckFromGroup(group), delta);
    } else {
        engine.setValue(group, "jog", delta); // pitch bend when the platter isn't touched
    }
};
