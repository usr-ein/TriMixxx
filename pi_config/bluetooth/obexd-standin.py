#!/usr/bin/env python3
"""obexd's stand-in, for an emulated deck: the covers' half of
bluez-standin.py. A phone's cover comes over Bluetooth through obexd's BIP
client, which needs a radio; this plays that client on the deck user's session
bus, enough for the receiver's cover fetch and for mpris-proxy's: sessions to
the stand-in's phones, and the image `s Cover` staged, for its handle. Like
the phones it stands for, each serves one client at all (Android's cover
server does): a second CreateSession to a phone is refused while a session to
it is open. A test tool, never installed on a deck by any deploy step.

As the deck user, with bluez-standin.py up:

    systemctl --user stop obex.service       # obexd out of the way
    python3 obexd-standin.py &               # owns org.bluez.obex in its place

then `s Cover s /tmp/cover.png` (bluez-standin.py) offers that picture as the
playing track's cover.
"""

import os
import shutil
import sys
import tempfile

from gi.repository import Gio, GLib

ROOT = "/org/bluez/obex"
STAGED = "/tmp/standin-covers"  # bluez-standin.py's `Cover`: one file per handle
IMAGE_UUID = "0000111A-0000-1000-8000-00805f9b34fb"  # obexd's Target for bip-avrcp

INTROSPECTION = """
<node>
  <interface name="org.freedesktop.DBus.ObjectManager">
    <method name="GetManagedObjects">
      <arg name="objects" type="a{oa{sa{sv}}}" direction="out"/>
    </method>
    <signal name="InterfacesAdded"><arg type="o"/><arg type="a{sa{sv}}"/></signal>
    <signal name="InterfacesRemoved"><arg type="o"/><arg type="as"/></signal>
  </interface>
  <interface name="org.bluez.obex.Client1">
    <method name="CreateSession">
      <arg type="s" direction="in"/><arg type="a{sv}" direction="in"/>
      <arg type="o" direction="out"/>
    </method>
    <method name="RemoveSession"><arg type="o" direction="in"/></method>
  </interface>
  <interface name="org.bluez.obex.Session1">
    <property name="Destination" type="s" access="read"/>
    <property name="PSM" type="q" access="read"/>
    <property name="Target" type="s" access="read"/>
  </interface>
  <interface name="org.bluez.obex.Image1">
    <method name="Get">
      <arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="a{sv}" direction="in"/>
      <arg type="o" direction="out"/><arg type="a{sv}" direction="out"/>
    </method>
    <method name="GetThumbnail">
      <arg type="s" direction="in"/><arg type="s" direction="in"/>
      <arg type="o" direction="out"/><arg type="a{sv}" direction="out"/>
    </method>
  </interface>
  <interface name="org.bluez.obex.Transfer1">
    <property name="Status" type="s" access="read"/>
    <property name="Name" type="s" access="read"/>
    <property name="Size" type="t" access="read"/>
    <property name="Filename" type="s" access="read"/>
    <property name="Session" type="o" access="read"/>
  </interface>
</node>
"""
NODE = Gio.DBusNodeInfo.new_for_xml(INTROSPECTION)
IFACES = {i.name: i for i in NODE.interfaces}
SIGNATURES = {i.name: {p.name: p.signature for p in i.properties} for i in NODE.interfaces}

bus = None
objects = {}        # path -> {interface: {property: GLib.Variant}}
registrations = {}  # path -> [registration ids]
owners = {}         # session path -> (owner's bus name, its name watch)
counter = 0


def v(interface, name, value):
    return GLib.Variant(SIGNATURES[interface][name], value)


def add(path, interfaces):
    objects.setdefault(path, {})
    registrations.setdefault(path, [])
    for interface, props in interfaces.items():
        objects[path][interface] = {n: v(interface, n, x) for n, x in props.items()}
        registrations[path].append(bus.register_object(
            path, IFACES[interface], on_method, on_get, None))
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


def changed(path, interface, **props):
    variants = {name: v(interface, name, value) for name, value in props.items()}
    objects[path][interface].update(variants)
    bus.emit_signal(None, path, "org.freedesktop.DBus.Properties", "PropertiesChanged",
                    GLib.Variant("(sa{sv}as)", (interface, variants, [])))


def on_get(conn, sender, path, interface, name):
    return objects[path][interface][name]


def fail(invocation, what):
    invocation.return_dbus_error("org.bluez.obex.Error.Failed", what)


def end_session(path):
    owner = owners.pop(path, None)
    if owner:
        Gio.bus_unwatch_name(owner[1])
    for transfer in [p for p in objects if p.startswith(path + "/")]:
        remove(transfer)
    remove(path)
    print(f"session {path} ended", flush=True)


def transfer(session, target, handle, invocation):
    """A transfer as obexd's: queued, then complete a moment later, as if the
    phone had sent it."""
    global counter
    staged = os.path.join(STAGED, handle)
    if not os.path.isfile(staged):
        return fail(invocation, f"Not Found: no image staged for handle {handle}")
    if not target:
        fd, target = tempfile.mkstemp(prefix="obex-standin-")
        os.close(fd)
    shutil.copyfile(staged, target)
    counter += 1
    path = f"{session}/transfer{counter}"
    props = {"Status": "queued", "Name": handle, "Size": os.path.getsize(target),
             "Filename": target, "Session": session}
    add(path, {"org.bluez.obex.Transfer1": props})
    invocation.return_value(GLib.Variant("(oa{sv})", (path, objects[path]["org.bluez.obex.Transfer1"])))

    def complete():
        if path in objects:
            changed(path, "org.bluez.obex.Transfer1", Status="complete")
            GLib.timeout_add(1000, lambda: remove(path) or False)
        return False
    GLib.timeout_add(300, complete)


def on_method(conn, sender, path, interface, method, params, invocation):
    global counter
    args = params.unpack()
    print(f"call {interface}.{method}{args} from {sender}", flush=True)
    if interface == "org.freedesktop.DBus.ObjectManager":
        invocation.return_value(GLib.Variant("(a{oa{sa{sv}}})", (objects,)))
    elif method == "CreateSession":
        destination, options = args
        if options.get("Target", "").lower() != "bip-avrcp":
            return fail(invocation, "the stand-in serves covers only (Target bip-avrcp)")
        if any(objects[open_]["org.bluez.obex.Session1"]["Destination"].unpack() == destination
               for open_ in owners):
            # One client at all, as Android's cover server: the phone refuses.
            return fail(invocation, "Connection refused (111)")
        counter += 1
        session = f"{ROOT}/client/session{counter}"
        add(session, {"org.bluez.obex.Session1": {"Destination": destination,
                                                  "PSM": options.get("PSM", 0),
                                                  "Target": IMAGE_UUID},
                      "org.bluez.obex.Image1": {}})
        # obexd ends a session whose client leaves the bus.
        watch = Gio.bus_watch_name_on_connection(
            bus, sender, Gio.BusNameWatcherFlags.NONE, None,
            lambda c, n, s=session: end_session(s))
        owners[session] = (sender, watch)
        print(f"session {session} to {destination}, for {sender}", flush=True)
        invocation.return_value(GLib.Variant("(o)", (session,)))
    elif method == "RemoveSession":
        if args[0] not in owners:
            return fail(invocation, "no such session")
        if owners[args[0]][0] != sender:
            return invocation.return_dbus_error("org.bluez.obex.Error.NotAuthorized",
                                                "Not Authorized")
        end_session(args[0])
        invocation.return_value(None)
    elif method == "GetThumbnail":
        transfer(path, args[0], args[1], invocation)
    elif method == "Get":
        transfer(path, args[0], args[1], invocation)


def on_bus(conn, name):
    global bus
    bus = conn
    registrations["/"] = [bus.register_object(
        "/", IFACES["org.freedesktop.DBus.ObjectManager"], on_method, None, None)]
    registrations[ROOT] = [bus.register_object(
        ROOT, IFACES["org.bluez.obex.Client1"], on_method, None, None)]


def on_name(conn, name):
    print(f"owning {name}: the stand-in is up", flush=True)


def on_lost(conn, name):
    sys.exit(f"could not own {name}: is obexd still running (systemctl --user stop obex)?")


Gio.bus_own_name(Gio.BusType.SESSION, "org.bluez.obex", Gio.BusNameOwnerFlags.NONE,
                 on_bus, on_name, on_lost)
GLib.MainLoop().run()
