#!/usr/bin/env python3
"""bluetoothd's stand-in, for an emulated deck: QEMU has no radio, so there is
no adapter, no phone and no BlueZ to show the Bluetooth receiver's page. This
plays enough of org.bluez for it -- an adapter, and a phone that pairs with a
code, connects, plays and pauses -- driven from a shell. A test tool, never
installed on a deck by any deploy step.

    sudo systemctl stop trimixxx-bluetoothd    # the menu open, bluetoothd up
    sudo python3 bluez-standin.py &            # owns org.bluez in its place

then, from any shell on the deck:

    s() { busctl call org.bluez /org/bluez org.trimixxx.StandIn "$@"; }
    s Pair s "Pixel 9"              # a phone asks to pair (Pair pressed first)
    s Connect s "Pixel 9"           # a paired phone connects
    s Play sssuu "Title" "Artist" "Album" 245000 61000   # ms: duration, position
    s Cover s /tmp/cover.jpg        # what the cover fetch would have landed
    s Pause
    s Disconnect

It answers the deck's own calls as BlueZ would: the agent's registration, a
phone's Connect (refused: no radio), Discoverable/Pairable/Trusted set, the
Pair window closing after DiscoverableTimeout.
"""

import sys

from gi.repository import Gio, GLib

ADAPTER = "/org/bluez/hci0"
DISCOVERABLE_TIMEOUT_S = 120
PASSKEY = 123456

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


def on_method(conn, sender, path, interface, method, params, invocation):
    global agent, phone
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
        fail(invocation, "no radio: this is the stand-in")
    elif method == "Pair":
        pair(args[0], invocation)
    elif method == "Connect":
        path = device_path(args[0])
        if not objects.get(path, {}).get("org.bluez.Device1", {}).get("Paired", GLib.Variant("b", False)).unpack():
            return fail(invocation, f"{args[0]} is not paired")
        phone = path
        remove(path + "/player0")  # connected already: anew
        remove(path + "/sep1/fd0")
        changed(path, "org.bluez.Device1", Connected=True)
        add(path + "/player0", {"org.bluez.MediaPlayer1": {
            "Track": {}, "Position": 0, "Status": "stopped", "Device": path, "ObexPort": 0}})
        add(path + "/sep1/fd0", {"org.bluez.MediaTransport1": {"State": "idle", "Device": path}})
        invocation.return_value(None)
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
        print("Cover: the receiver fetches covers through obexd, which a stand-in has none of",
              flush=True)
        invocation.return_value(None)
    elif method == "Disconnect":
        if phone:
            remove(phone + "/player0")
            remove(phone + "/sep1/fd0")
            changed(phone, "org.bluez.Device1", Connected=False)
            phone = None
        invocation.return_value(None)


def pair(name, invocation):
    if not objects[ADAPTER]["org.bluez.Adapter1"]["Discoverable"].unpack():
        return fail(invocation, "the deck is not visible: press Pair on it first")
    if not agent:
        return fail(invocation, "the deck registered no agent")
    path = device_path(name)
    if path not in objects:
        add(path, {"org.bluez.Device1": {"Address": "AA:BB:CC:DD:EE:FF", "Alias": name,
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
