// ===========================================================================
//  PiMidiDaemon.scripts.js  -  turns Mixxx controls into system actions.
//
//  Mixxx cannot touch the OS: controller scripts run in a bare QJSEngine whose
//  whole API is control values, timers and MIDI I/O, and skins are XML/QSS. So
//  the skin's POWER menu only sets [TriMixxx],shutdown_now, and this script
//  forwards that to the pi-midi-daemon service as SysEx, which runs the actual
//  `systemctl poweroff`. Protocol: F0 7D <opcode> F7 (pi-midi-daemon/README.md).
//
//  Why this is its own mapping rather than part of TriMixxx.scripts.js: a
//  controller script's `midi` object is hard-bound to its OWN controller's port
//  (MidiControllerJSProxy wraps one MidiController), so only a script loaded on
//  the pi-midi-daemon device can send to the daemon.
//
//  Why a script rather than an <output>: an <output> can only emit a 3-byte
//  message. Commands are SysEx, so they need midi.sendSysexMsg().
// ===========================================================================
var PiMidiDaemon = {};

// SysEx ID 0x7D is reserved by the MIDI spec for non-commercial / educational
// use, so it cannot collide with a real vendor. Must match main.go's sysExID.
PiMidiDaemon.SYSEX_ID     = 0x7D;

// Opcodes. Mixxx -> daemon (commands we send); must match main.go's `actions`:
PiMidiDaemon.CMD_PING     = 0x00;
PiMidiDaemon.CMD_SHUTDOWN = 0x01;
PiMidiDaemon.CMD_REBOOT   = 0x02;
// daemon -> Mixxx (events we receive); must match main.go's evtUSB* consts.
// Numbered from 0x10 to keep the two directions apart in a MIDI dump. The
// daemon polls /media/DJ_USB_* and fires these as slots appear and vanish.
PiMidiDaemon.EVT_USB_MOUNTED   = 0x10;
PiMidiDaemon.EVT_USB_UNMOUNTED = 0x11;

// The skin creates these controls via its <attribute> block.
PiMidiDaemon.GROUP    = "[TriMixxx]";
PiMidiDaemon.RETRY_MS = 500;

PiMidiDaemon.init = function(id, debugging) {
    PiMidiDaemon.connect();
};

PiMidiDaemon.shutdown = function() {
    if (PiMidiDaemon.conn) { PiMidiDaemon.conn.disconnect(); }
};

// [TriMixxx],* only exists once the SKIN has loaded, and Mixxx may open this
// controller first -- makeConnection then returns undefined and the button would
// silently never work. So retry until the control exists.
PiMidiDaemon.connect = function() {
    var conn = engine.makeConnection(PiMidiDaemon.GROUP, "shutdown_now", PiMidiDaemon.onShutdown);
    if (conn) {
        PiMidiDaemon.conn = conn;
        return;
    }
    engine.beginTimer(PiMidiDaemon.RETRY_MS, PiMidiDaemon.connect, true);
};

// The skin's SHUT DOWN button pulses 1 on press and 0 on release; act on the
// rising edge only, or releasing would fire a second shutdown.
PiMidiDaemon.onShutdown = function(value) {
    if (!value) { return; }
    PiMidiDaemon.send(PiMidiDaemon.CMD_SHUTDOWN);
};

PiMidiDaemon.send = function(opcode) {
    var msg = [0xF0, PiMidiDaemon.SYSEX_ID, opcode, 0xF7];
    midi.sendSysexMsg(msg, msg.length);
};

// ---- daemon -> Mixxx -------------------------------------------------------
// Mixxx routes SysEx by convention, not by the mapping's <key>: it keys the
// message as (status 0xF0, control 0xFF), then calls "<prefix>.incomingData"
// for EVERY script prefix on this controller. So this name is load-bearing --
// renaming it silently stops the deck refreshing.
//   handleIncomingData() passes (data, data.size()).
PiMidiDaemon.incomingData = function(data, length) {
    // F0 7D <opcode> F7. Validate the frame rather than trusting index 2: this
    // handler sees every SysEx the port receives, not only ours.
    if (length !== 4) { return; }
    if (data[0] !== 0xF0 || data[1] !== PiMidiDaemon.SYSEX_ID || data[3] !== 0xF7) { return; }

    // Both directions of the mount matter. Mounting brings a stick's playlists
    // in; unmounting is what clears a yanked stick back out, since Mixxx keeps
    // showing a device that is physically gone until something rescans.
    // findRekordboxDevices() rebuilds the list either way, so one handler does.
    switch (data[2]) {
    case PiMidiDaemon.EVT_USB_MOUNTED:
    case PiMidiDaemon.EVT_USB_UNMOUNTED:
        PiMidiDaemon.refreshRekordbox();
        break;
    }
};

// Rescan the attached Rekordbox USB / SD devices.
//
// There is no ControlObject for this -- RekordboxFeature has none at all. The
// only reachable path is the sidebar: [Playlist],ToggleSelectedSidebarItem ->
// WLibrarySidebar::toggleSelectedItem() -> emit clicked(index) ->
// RekordboxFeature::activate() -> QtConcurrent::run(findRekordboxDevices).
// That is the exact same activate() the "Check for attached Rekordbox USB / SD
// devices (refresh)" link calls, so this is a real refresh, not an approximation.
//
// The catch, and it is a real one: "Selected" is literal. This acts on whatever
// sidebar item is currently selected, and no Mixxx control can select a feature
// by name (librarycontrol.cpp offers relative navigation only). So this
// refreshes Rekordbox only while Rekordbox is the selected sidebar item -- the
// deck's normal resting state, but an assumption. If something else is
// selected, this activates that instead; if nothing is selected (the library
// was never opened), selectedIndex() is invalid and it is a no-op.
// Side effect: toggleSelectedItem() also flips the item's expanded state.
PiMidiDaemon.refreshRekordbox = function() {
    engine.setValue("[Playlist]", "ToggleSelectedSidebarItem", 1);
};
