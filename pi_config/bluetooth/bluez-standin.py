#!/usr/bin/env python3
"""bluetoothd's stand-in, for an emulated deck: QEMU has no radio, so there is
no adapter, no phone and no BlueZ to show the Bluetooth receiver's page. This
plays enough of org.bluez for it -- an adapter, and phones that pair with a
code, connect, play and pause -- driven from a shell. A test tool, never
installed on a deck by any deploy step.

    sudo systemctl stop trimixxx-bluetoothd    # the menu open, bluetoothd up
    sudo python3 bluez-standin.py &            # owns org.bluez in its place

then, from any shell on the deck:

    s() { busctl call org.bluez /org/bluez org.trimixxx.StandIn "$@"; }
    s Pair s "Pixel 9"              # a phone asks to pair (Pair pressed first)
    s Connect s "Pixel 9"           # a paired phone tries to connect by itself
    s Play sssuu "Title" "Artist" "Album" 245000 61000   # ms: duration, position
    s Cover s /tmp/cover.png        # the phone offers this picture as the cover
    s Pause
    s Disconnect                    # the phone playing goes

A phone connecting by itself gets in as with BlueZ: at once if trusted, else
the deck's agent is asked (AuthorizeService), which may refuse it. The deck's
own calls are answered as BlueZ would: the agent's registration, a paired
device's Connect and Disconnect, RemoveDevice, Discoverable/Pairable/Trusted
set, the Pair window closing after DiscoverableTimeout. The covers come
through obexd, which obexd-standin.py stands in for: `Cover` stages the
picture for it, under the handle the track then carries.
"""

import os
import shutil
import sys

from gi.repository import Gio, GLib

ADAPTER = "/org/bluez/hci0"
DISCOVERABLE_TIMEOUT_S = 120
PASSKEY = 123456
COVER_PSM = 4103                # where a phone's cover server listens: any but 0
AUDIO_SINK_UUID = "0000110b-0000-1000-8000-00805f9b34fb"  # what a phone connects to
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
    <method name="Pause"/>
    <method name="Disconnect"/>
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
phone = None       # the phone's device path, once paired
track = {}         # the playing track, as its player's Track
covers = 0         # covers offered so far; from the first, the player has ObexPort
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
    """A phone's player as BlueZ makes it: ObexPort only when the phone offers
    covers, which BlueZ reads once, as the player appears (so does mpris-proxy)."""
    props = {"Track": track, "Position": position, "Status": status, "Device": device}
    if covers:
        props["ObexPort"] = COVER_PSM
    return props


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
    global phone, track
    phone = path
    remove(path + "/player0")  # connected already: anew
    remove(path + "/sep1/fd0")
    changed(path, "org.bluez.Device1", Connected=True)
    track = {}
    add(path + "/player0", {"org.bluez.MediaPlayer1": player_props(path)})
    add(path + "/sep1/fd0", {"org.bluez.MediaTransport1": {"State": "idle", "Device": path}})
    print(f"connected {path}", flush=True)


def disconnect(path):
    global phone
    if not objects.get(path, {}).get("org.bluez.Device1", {}).get(
            "Connected", GLib.Variant("b", False)).unpack():
        return
    remove(path + "/player0")
    remove(path + "/sep1/fd0")
    changed(path, "org.bluez.Device1", Connected=False)
    if phone == path:
        phone = None
    print(f"disconnected {path}", flush=True)


def on_method(conn, sender, path, interface, method, params, invocation):
    global agent, phone, track, covers
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
        if method == "Connect":
            connect(path)
        else:
            disconnect(path)
        invocation.return_value(None)
    elif interface == "org.bluez.Adapter1" and method == "RemoveDevice":
        device = args[0]
        if device not in objects:
            return invocation.return_dbus_error("org.bluez.Error.DoesNotExist", "no such device")
        disconnect(device)
        remove(device)
        print(f"forgot {device}", flush=True)
        invocation.return_value(None)
    elif method == "Pair":
        pair(args[0], invocation)
    elif method == "Connect":
        # The phone, by itself: trusted gets in, else the deck is asked.
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
        if not phone:
            return fail(invocation, "no phone connected")
        title, artist, album, duration, position = args
        track = {"Title": GLib.Variant("s", title), "Artist": GLib.Variant("s", artist),
                 "Album": GLib.Variant("s", album), "Duration": GLib.Variant("u", duration)}
        changed(phone + "/player0", "org.bluez.MediaPlayer1",
                Track=track, Position=position, Status="playing")
        changed(phone + "/sep1/fd0", "org.bluez.MediaTransport1", State="active")
        invocation.return_value(None)
    elif method == "Pause":
        if not phone:
            return fail(invocation, "no phone connected")
        changed(phone + "/player0", "org.bluez.MediaPlayer1", Status="paused")
        changed(phone + "/sep1/fd0", "org.bluez.MediaTransport1", State="idle")
        invocation.return_value(None)
    elif method == "Cover":
        if not phone:
            return fail(invocation, "no phone connected")
        covers += 1
        handle = str(1000000 + covers)
        os.makedirs(STAGED, mode=0o755, exist_ok=True)
        shutil.copyfile(args[0], os.path.join(STAGED, handle))
        os.chmod(os.path.join(STAGED, handle), 0o644)
        track = dict(track, ImgHandle=GLib.Variant("s", handle))
        player = phone + "/player0"
        now = objects[player]["org.bluez.MediaPlayer1"]
        if "ObexPort" in now:
            changed(player, "org.bluez.MediaPlayer1", Track=track)
        else:
            # The first cover: a player with ObexPort, as BlueZ would have made
            # it from the start for a phone that offers covers.
            position, status = now["Position"].unpack(), now["Status"].unpack()
            remove(player)
            add(player, {"org.bluez.MediaPlayer1": player_props(phone, position, status)})
        print(f"cover {handle}: {args[0]}", flush=True)
        invocation.return_value(None)
    elif method == "Disconnect":
        if phone:
            disconnect(phone)
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

    # The phone shows the code, and so must the deck: numeric comparison.
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
