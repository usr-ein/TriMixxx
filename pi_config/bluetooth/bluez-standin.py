#!/usr/bin/env python3
"""bluetoothd's stand-in, for an emulated deck: QEMU has no radio, so there is
no adapter, no device and no BlueZ to show the Bluetooth receiver's page. This
plays enough of org.bluez for it -- an adapter, and devices that pair with a
code, connect, play, pause, seek and skip, or are out of range -- driven from
a shell. A test tool, never installed on a deck by any deploy step.

    sudo systemctl stop trimixxx-bluetoothd    # the menu open, bluetoothd up
    sudo python3 bluez-standin.py &            # owns org.bluez in its place

then, from any shell on the deck:

    s() { busctl call org.bluez /org/bluez org.trimixxx.StandIn "$@"; }
    s Pair s "Pixel 9"              # a device asks to pair (+ pressed first)
    s Connect s "Pixel 9"           # a paired device tries to connect by itself
    s Play sssuu "Title" "Artist" "Album" 245000 61000   # ms: duration, position
    s Cover s /tmp/cover.png        # the device's covers, from the next track on
    s Seek u 0                      # the device seeks: back to the start
    s Status s paused               # its own play/pause: paused, playing...
    s Pause
    s Away s "Pixel 9"              # out of range: the deck's Connect times out
    s Near s "Pixel 9"              # back in range
    s Disconnect                    # the device playing goes

A device connecting by itself gets in as with BlueZ: at once if trusted, else
the deck's agent is asked (AuthorizeService), which may refuse it. The deck's
own calls are answered as BlueZ would: the agent's registration, a paired
device's Connect (after BlueZ's page timeout for one away) and Disconnect,
RemoveDevice, Discoverable/Pairable/Trusted set, the Pair window closing after
DiscoverableTimeout, and the player's Play, Pause, Next and Previous (the
deck's PLAY, loop halve and loop double), which play, pause or skip as the
device would, and say so.

Covers come as from an iPhone (AVRCP 1.6): the player says where the device
serves them (ObexPort) from the start, and a track carries a cover's handle
only while a cover session to the device is open -- obexd-standin.py tells
this one when it opens or ends one (CoverSession) -- and only from the next
time the track's attributes are read: a new track (Play, Next, Previous).
`Cover` stages the picture those tracks get.
"""

import os
import shutil
import sys

from gi.repository import Gio, GLib

ADAPTER = "/org/bluez/hci0"
DISCOVERABLE_TIMEOUT_S = 120
PAGE_TIMEOUT_MS = 5120          # BlueZ's: a Connect to a device away fails after it
PASSKEY = 123456
COVER_PSM = 4103                # where a device's cover server listens: any but 0
AUDIO_SINK_UUID = "0000110b-0000-1000-8000-00805f9b34fb"  # what a device connects to
STAGED = "/tmp/standin-covers"  # obexd-standin.py serves these, by handle

INTROSPECTION = """
<node>
  <interface name="org.freedesktop.DBus.ObjectManager">
    <method name="GetManagedObjects">
      <arg name="objects" type="a{oa{sa{sv}}}" direction="out"/>
    </method>
    <signal name="InterfacesAdded"><arg type="o"/><arg type="a{sa{sv}}"/></signal>
    <signal name="InterfacesRemoved"><arg type="o"/><arg type="as"/></signal>
  </interface>
  <interface name="org.bluez.AgentManager1">
    <method name="RegisterAgent"><arg type="o" direction="in"/><arg type="s" direction="in"/></method>
    <method name="UnregisterAgent"><arg type="o" direction="in"/></method>
    <method name="RequestDefaultAgent"><arg type="o" direction="in"/></method>
  </interface>
  <interface name="org.trimixxx.StandIn">
    <method name="Pair"><arg type="s" direction="in"/></method>
    <method name="Connect"><arg type="s" direction="in"/></method>
    <method name="Play">
      <arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="s" direction="in"/>
      <arg type="u" direction="in"/><arg type="u" direction="in"/>
    </method>
    <method name="Cover"><arg type="s" direction="in"/></method>
    <method name="Seek"><arg type="u" direction="in"/></method>
    <method name="Status"><arg type="s" direction="in"/></method>
    <method name="Pause"/>
    <method name="Away"><arg type="s" direction="in"/></method>
    <method name="Near"><arg type="s" direction="in"/></method>
    <method name="Disconnect"/>
    <method name="CoverSession"><arg type="s" direction="in"/><arg type="b" direction="in"/></method>
  </interface>
  <interface name="org.bluez.Adapter1">
    <property name="Address" type="s" access="read"/>
    <property name="Alias" type="s" access="readwrite"/>
    <property name="Powered" type="b" access="readwrite"/>
    <property name="Discoverable" type="b" access="readwrite"/>
    <property name="Pairable" type="b" access="readwrite"/>
    <property name="DiscoverableTimeout" type="u" access="readwrite"/>
    <method name="RemoveDevice"><arg type="o" direction="in"/></method>
  </interface>
  <interface name="org.bluez.Device1">
    <method name="Connect"/>
    <method name="Disconnect"/>
    <property name="Address" type="s" access="read"/>
    <property name="Alias" type="s" access="readwrite"/>
    <property name="Paired" type="b" access="read"/>
    <property name="Trusted" type="b" access="readwrite"/>
    <property name="Connected" type="b" access="read"/>
    <property name="Adapter" type="o" access="read"/>
  </interface>
  <interface name="org.bluez.MediaPlayer1">
    <method name="Play"/>
    <method name="Pause"/>
    <method name="Next"/>
    <method name="Previous"/>
    <property name="Track" type="a{sv}" access="read"/>
    <property name="Position" type="u" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="Device" type="o" access="read"/>
    <property name="ObexPort" type="q" access="read"/>
  </interface>
  <interface name="org.bluez.MediaTransport1">
    <property name="State" type="s" access="read"/>
    <property name="Device" type="o" access="read"/>
  </interface>
</node>
"""
NODE = Gio.DBusNodeInfo.new_for_xml(INTROSPECTION)
IFACES = {i.name: i for i in NODE.interfaces}
SIGNATURES = {i.name: {p.name: p.signature for p in i.properties} for i in NODE.interfaces}

bus = None
objects = {}       # path -> {interface: {property: GLib.Variant}}
registrations = {}  # path -> [registration ids]
agent = None       # (bus name, object path) of the deck's agent
connected = None   # the connected device's path
track = {}         # the playing track, as its player's Track
covers = 0         # covers handed out so far: each track's handle is a new one
cover_file = None  # the picture `Cover` staged, for the tracks to come
sessions = set()   # the addresses with a cover session open (obexd-standin.py)
away = set()       # the device paths out of range
discoverable_timer = 0


def v(interface, name, value):
    return GLib.Variant(SIGNATURES[interface][name], value)


def changed(path, interface, **props):
    """Set properties and say so, as bluetoothd does."""
    variants = {name: v(interface, name, value) for name, value in props.items()}
    objects[path][interface].update(variants)
    bus.emit_signal(None, path, "org.freedesktop.DBus.Properties", "PropertiesChanged",
                    GLib.Variant("(sa{sv}as)", (interface, variants, [])))


def add(path, interfaces):
    """An object with its interfaces' properties: {interface: {name: value}}."""
    objects.setdefault(path, {})
    registrations.setdefault(path, [])
    for interface, props in interfaces.items():
        objects[path][interface] = {n: v(interface, n, x) for n, x in props.items()}
        registrations[path].append(bus.register_object(
            path, IFACES[interface], on_method, on_get, on_set))
    bus.emit_signal(None, "/", "org.freedesktop.DBus.ObjectManager", "InterfacesAdded",
                    GLib.Variant("(oa{sa{sv}})", (path, objects[path])))


def remove(path):
    if path not in objects:
        return
    names = list(objects[path])
    for rid in registrations.pop(path, []):
        bus.unregister_object(rid)
    del objects[path]
    bus.emit_signal(None, "/", "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved",
                    GLib.Variant("(oas)", (path, names)))


def player_props(device, position=0, status="stopped"):
    """A device's player as BlueZ makes it, with ObexPort: the device offers
    covers, and BlueZ reads where from its SDP record as the player appears
    (in experimental mode only, which bluetooth-main.conf sets)."""
    return {"Track": track, "Position": position, "Status": status, "Device": device,
            "ObexPort": COVER_PSM}


def address(path):
    return objects[path]["org.bluez.Device1"]["Address"].unpack()


def new_track(title, artist, album, duration, number=1):
    """A track as read from the device: with a cover's handle only while a
    cover session to it is open (AVRCP 1.6), and a picture is staged."""
    global covers
    t = {"Title": GLib.Variant("s", title), "Artist": GLib.Variant("s", artist),
         "Album": GLib.Variant("s", album), "Duration": GLib.Variant("u", duration),
         "TrackNumber": GLib.Variant("u", number)}
    if cover_file and connected and address(connected) in sessions:
        covers += 1
        handle = str(1000000 + covers)
        os.makedirs(STAGED, mode=0o755, exist_ok=True)
        shutil.copyfile(cover_file, os.path.join(STAGED, handle))
        os.chmod(os.path.join(STAGED, handle), 0o644)
        t["ImgHandle"] = GLib.Variant("s", handle)
        print(f"cover {handle} with {title}", flush=True)
    return t


def skip(step):
    """Next (+1) or Previous (-1), as the device does them: another track, from
    its start, playing."""
    global track
    number = track.get("TrackNumber", GLib.Variant("u", 1)).unpack() + step
    number = max(1, number)
    duration = track.get("Duration", GLib.Variant("u", 200000)).unpack()
    artist = track.get("Artist", GLib.Variant("s", "")).unpack()
    album = track.get("Album", GLib.Variant("s", "")).unpack()
    track = new_track(f"Track {number}", artist, album, duration, number)
    changed(connected + "/player0", "org.bluez.MediaPlayer1", Track=track)
    changed(connected + "/player0", "org.bluez.MediaPlayer1", Position=0, Status="playing")
    changed(connected + "/sep1/fd0", "org.bluez.MediaTransport1", State="active")


def device_path(name):
    return ADAPTER + "/dev_" + "".join(c if c.isalnum() else "_" for c in name).upper()[:17]


def on_get(conn, sender, path, interface, name):
    return objects[path][interface][name]


def on_set(conn, sender, path, interface, name, value):
    global discoverable_timer
    changed(path, interface, **{name: value.unpack()})
    if interface == "org.bluez.Adapter1" and name == "Discoverable":
        if discoverable_timer:
            GLib.source_remove(discoverable_timer)
            discoverable_timer = 0
        if value.unpack():
            def end():
                global discoverable_timer
                discoverable_timer = 0
                changed(ADAPTER, "org.bluez.Adapter1", Discoverable=False)
                return False
            discoverable_timer = GLib.timeout_add_seconds(DISCOVERABLE_TIMEOUT_S, end)
    print(f"set {path} {interface}.{name} = {value.unpack()}", flush=True)
    return True


def fail(invocation, what):
    invocation.return_dbus_error("org.bluez.Error.Failed", what)


def paired(path):
    return objects.get(path, {}).get("org.bluez.Device1", {}).get(
        "Paired", GLib.Variant("b", False)).unpack()


def connect(path):
    """A paired device in: its player and its audio transport, as BlueZ adds them."""
    global connected, track
    connected = path
    remove(path + "/player0")  # connected already: anew
    remove(path + "/sep1/fd0")
    changed(path, "org.bluez.Device1", Connected=True)
    track = {}
    add(path + "/player0", {"org.bluez.MediaPlayer1": player_props(path)})
    add(path + "/sep1/fd0", {"org.bluez.MediaTransport1": {"State": "idle", "Device": path}})
    print(f"connected {path}", flush=True)


def disconnect(path):
    global connected
    if not objects.get(path, {}).get("org.bluez.Device1", {}).get(
            "Connected", GLib.Variant("b", False)).unpack():
        return
    remove(path + "/player0")
    remove(path + "/sep1/fd0")
    changed(path, "org.bluez.Device1", Connected=False)
    if connected == path:
        connected = None
    print(f"disconnected {path}", flush=True)


def on_method(conn, sender, path, interface, method, params, invocation):
    global agent, connected, track, cover_file
    args = params.unpack()
    print(f"call {interface}.{method}{args} from {sender}", flush=True)
    if interface == "org.freedesktop.DBus.ObjectManager":
        invocation.return_value(GLib.Variant("(a{oa{sa{sv}}})", (objects,)))
    elif interface == "org.bluez.AgentManager1":
        if method == "RegisterAgent":
            agent = (sender, args[0])
        elif method == "UnregisterAgent" and agent and agent[1] == args[0]:
            agent = None
        invocation.return_value(None)
    elif interface == "org.bluez.Device1":
        # The deck's own: connect a paired device, or let one go.
        if not paired(path):
            return fail(invocation, "not paired")
        if method == "Connect" and path in away:
            # Paged, and nothing answers: BlueZ gives up after its page timeout.
            def timed_out():
                print(f"{path}: page timeout", flush=True)
                fail(invocation, "br-connection-page-timeout")
                return False
            GLib.timeout_add(PAGE_TIMEOUT_MS, timed_out)
            return
        if method == "Connect":
            connect(path)
        else:
            disconnect(path)
        invocation.return_value(None)
    elif interface == "org.bluez.MediaPlayer1":
        # The deck's PLAY, loop halve and loop double: AVRCP's keys, which the
        # device acts on, then says what it is doing.
        print(f"the deck asked: {method}", flush=True)
        if method == "Play":
            changed(path, "org.bluez.MediaPlayer1", Status="playing")
            changed(connected + "/sep1/fd0", "org.bluez.MediaTransport1", State="active")
        elif method == "Pause":
            changed(path, "org.bluez.MediaPlayer1", Status="paused")
            changed(connected + "/sep1/fd0", "org.bluez.MediaTransport1", State="idle")
        elif method in ("Next", "Previous"):
            skip(1 if method == "Next" else -1)
        invocation.return_value(None)
    elif interface == "org.bluez.Adapter1" and method == "RemoveDevice":
        device = args[0]
        if device not in objects:
            return invocation.return_dbus_error("org.bluez.Error.DoesNotExist", "no such device")
        disconnect(device)
        remove(device)
        away.discard(device)
        print(f"forgot {device}", flush=True)
        invocation.return_value(None)
    elif method == "Pair":
        pair(args[0], invocation)
    elif method == "Connect":
        # The device, by itself: trusted gets in, else the deck is asked.
        path = device_path(args[0])
        if not paired(path):
            return fail(invocation, f"{args[0]} is not paired")
        if objects[path]["org.bluez.Device1"]["Trusted"].unpack():
            connect(path)
            return invocation.return_value(None)
        if not agent:
            return fail(invocation, "not trusted, and no agent to ask")

        def answered(conn, result):
            try:
                conn.call_finish(result)
            except GLib.Error as e:
                print(f"{args[0]} refused by the deck: {e.message}", flush=True)
                return fail(invocation, f"refused by the deck: {e.message}")
            connect(path)
            invocation.return_value(None)
        bus.call(agent[0], agent[1], "org.bluez.Agent1", "AuthorizeService",
                 GLib.Variant("(os)", (path, AUDIO_SINK_UUID)), None, Gio.DBusCallFlags.NONE,
                 60000, None, answered)
    elif method == "Play":
        if not connected:
            return fail(invocation, "no device connected")
        title, artist, album, duration, position = args
        track = new_track(title, artist, album, duration)
        changed(connected + "/player0", "org.bluez.MediaPlayer1", Track=track)
        changed(connected + "/player0", "org.bluez.MediaPlayer1", Position=position, Status="playing")
        changed(connected + "/sep1/fd0", "org.bluez.MediaTransport1", State="active")
        invocation.return_value(None)
    elif method == "Pause":
        if not connected:
            return fail(invocation, "no device connected")
        changed(connected + "/player0", "org.bluez.MediaPlayer1", Status="paused")
        changed(connected + "/sep1/fd0", "org.bluez.MediaTransport1", State="idle")
        invocation.return_value(None)
    elif method == "Status":
        # The device's own play/pause: BlueZ hears STATUS_CHANGED, then asks
        # its play status, which brings the position too.
        if not connected:
            return fail(invocation, "no device connected")
        player = connected + "/player0"
        changed(player, "org.bluez.MediaPlayer1", Status=args[0])
        changed(connected + "/sep1/fd0", "org.bluez.MediaTransport1",
                State="active" if args[0] == "playing" else "idle")
        invocation.return_value(None)
    elif method == "Seek":
        # A seek the device reports (PLAYBACK_POS_CHANGED): the position jumps.
        if not connected:
            return fail(invocation, "no device connected")
        changed(connected + "/player0", "org.bluez.MediaPlayer1", Position=args[0])
        invocation.return_value(None)
    elif method == "Cover":
        # The device's covers, for the tracks to come (each its own handle,
        # while a cover session is open).
        if not os.path.isfile(args[0]):
            return fail(invocation, f"no such file: {args[0]}")
        cover_file = args[0]
        print(f"covers from {args[0]}, from the next track on", flush=True)
        invocation.return_value(None)
    elif method == "CoverSession":
        # obexd-standin.py: a cover session to the device at this address
        # opened (True) or ended.
        addr, opened = args
        (sessions.add if opened else sessions.discard)(addr)
        print(f"cover session to {addr}: {'open' if opened else 'ended'}", flush=True)
        invocation.return_value(None)
    elif method in ("Away", "Near"):
        path = device_path(args[0])
        if path not in objects:
            return fail(invocation, f"no device {args[0]}")
        if method == "Away":
            away.add(path)
            disconnect(path)
        else:
            away.discard(path)
        print(f"{args[0]}: {method.lower()}", flush=True)
        invocation.return_value(None)
    elif method == "Disconnect":
        if connected:
            disconnect(connected)
        invocation.return_value(None)


def pair(name, invocation):
    if not objects[ADAPTER]["org.bluez.Adapter1"]["Discoverable"].unpack():
        return fail(invocation, "the deck is not visible: press Pair on it first")
    if not agent:
        return fail(invocation, "the deck registered no agent")
    path = device_path(name)
    if path not in objects:
        tail = ":".join(f"{b:02X}" for b in name.encode()[-3:].rjust(3, b"\0"))
        add(path, {"org.bluez.Device1": {"Address": "AA:BB:CC:" + tail, "Alias": name,
                                         "Paired": False, "Trusted": False,
                                         "Connected": False, "Adapter": ADAPTER}})

    def answered(conn, result):
        try:
            conn.call_finish(result)
        except GLib.Error as e:
            print(f"pairing refused: {e.message}", flush=True)
            invocation.return_dbus_error("org.bluez.Error.AuthenticationRejected", e.message)
            return
        changed(path, "org.bluez.Device1", Paired=True)
        print(f"paired with {name}", flush=True)
        invocation.return_value(None)

    # The device shows the code, and so must the deck: numeric comparison.
    bus.call(agent[0], agent[1], "org.bluez.Agent1", "RequestConfirmation",
             GLib.Variant("(ou)", (path, PASSKEY)), None, Gio.DBusCallFlags.NONE, 60000, None,
             answered)


def on_bus(conn, name):
    global bus
    bus = conn
    registrations["/"] = [bus.register_object("/", IFACES["org.freedesktop.DBus.ObjectManager"],
                                              on_method, None, None)]
    registrations["/org/bluez"] = [
        bus.register_object("/org/bluez", IFACES["org.bluez.AgentManager1"], on_method, None, None),
        bus.register_object("/org/bluez", IFACES["org.trimixxx.StandIn"], on_method, None, None)]
    add(ADAPTER, {"org.bluez.Adapter1": {"Address": "DC:A6:32:00:00:01", "Alias": GLib.get_host_name(),
                                         "Powered": True, "Discoverable": False, "Pairable": False,
                                         "DiscoverableTimeout": DISCOVERABLE_TIMEOUT_S}})


def on_name(conn, name):
    print(f"owning {name}: the stand-in is up", flush=True)


def on_lost(conn, name):
    sys.exit(f"could not own {name}: is bluetoothd still running?")


Gio.bus_own_name(Gio.BusType.SYSTEM, "org.bluez", Gio.BusNameOwnerFlags.NONE,
                 on_bus, on_name, on_lost)
GLib.MainLoop().run()
