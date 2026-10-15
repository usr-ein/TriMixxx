#include "decks/cable.h"

#include "decks/deck.h"
#include "util/fail.h"
#include "util/print.h"
#include "util/process.h"
#include "util/tool.h"

#include <QRegularExpression>

#include <algorithm>

#include <netdb.h>
#include <sys/socket.h>

namespace cable {

namespace {

// `ssh -G` prints "key value" lines.
QString value(const QString& sshG, const QString& key) {
    for (const QString& line : sshG.split('\n')) {
        const QString t = line.trimmed();
        if (t.section(' ', 0, 0).toLower() == key) return t.section(' ', 1);
    }
    return {};
}

bool isSet(const QString& v) { return !v.isEmpty() && v != "none"; }

proc::Result quietly(const QString& program, const QStringList& args) {
    proc::Options o;
    o.quiet = true;
    o.timeoutMs = 15000;
    return proc::capture(program, args, o);
}

// An interface's kind; a bridge's from its members': wired when they all are.
Kind kindOf(const QString& interface, const Lookups& l, QString* what, bool member = false) {
    QStringList members;
    const Kind k = interfaceKind(l.interfaceInfo(interface), what, &members);
    if (k != Kind::Unknown || members.isEmpty() || member) return k;
    for (const QString& m : members) {
        QString w;
        if (kindOf(m, l, &w, true) != Kind::Wired) {
            *what = "a bridge with " + m + " in it (" + w + ")";
            return Kind::Unknown;
        }
    }
    *what = "a bridge of " + members.join(", ") + ", all wired";
    return Kind::Wired;
}

QString describe(const Hop& h) {
    return h.interface.isEmpty() ? h.address + " (" + h.what + ")"
                                 : h.address + " through " + h.interface + " (" + h.what + ")";
}

QString describe(const QVector<Hop>& hops, std::initializer_list<Kind> kinds) {
    QStringList out;
    for (const Hop& h : hops)
        if (std::find(kinds.begin(), kinds.end(), h.kind) != kinds.end()) out << describe(h);
    return out.join(", ");
}

} // namespace

QString routeInterface(const QString& routeGet) {
    static const QRegularExpression re(R"(^\s*interface:\s*(\S+)\s*$)", QRegularExpression::MultilineOption);
    return re.match(routeGet).captured(1);
}

Kind interfaceKind(const QString& ifconfigV, QString* what, QStringList* members) {
    static const QRegularExpression flags(R"(flags=[0-9a-fA-F]+<([^>]*)>)");
    static const QRegularExpression type(R"(^\s*type:\s*(.+?)\s*$)", QRegularExpression::MultilineOption);
    static const QRegularExpression member(R"(^\s*member:\s*(\S+))", QRegularExpression::MultilineOption);
    if (ifconfigV.trimmed().isEmpty()) {
        *what = "not there";
        return Kind::Unknown;
    }
    // Its first line: "en0: flags=8863<UP,BROADCAST,...> mtu 1500".
    if (flags.match(ifconfigV.section('\n', 0, 0)).captured(1).split(',').contains("LOOPBACK")) {
        *what = "loopback";
        return Kind::Loopback;
    }
    // The kernel's kind of interface (ifconfig's ift2str): Ethernet, USB
    // Ethernet, Wi-Fi, IP over Thunderbolt, Bluetooth PAN, Cellular...
    const QString t = type.match(ifconfigV).captured(1);
    *what = t;
    if (t == "Wi-Fi") return Kind::WiFi;
    if (t == "Ethernet" || t == "USB Ethernet") return Kind::Wired;
    *what = t.isEmpty() ? "of no kind ifconfig names: a VPN's tunnel?" : t + ": neither Ethernet nor Wi-Fi";
    if (members)
        for (auto it = member.globalMatch(ifconfigV); it.hasNext();) *members << it.next().captured(1);
    return Kind::Unknown;
}

Lookups system() {
    Lookups l;
    l.sshConfig = [](const QString& alias) { return quietly("ssh", {"-G", alias}).text(); };
    l.resolve = [](const QString& host, const QString& family) {
        // As ssh's own resolve_host(): its AddressFamily, a stream socket,
        // the addresses in the order it tries them.
        addrinfo hints{};
        hints.ai_family = family == "inet" ? AF_INET : family == "inet6" ? AF_INET6 : AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* found = nullptr;
        QStringList out;
        if (getaddrinfo(host.toUtf8().constData(), "22", &hints, &found) != 0) return out;
        for (const addrinfo* a = found; a; a = a->ai_next) {
            char text[NI_MAXHOST];
            if (getnameinfo(a->ai_addr, a->ai_addrlen, text, sizeof text, nullptr, 0, NI_NUMERICHOST) != 0) continue;
            const QString address = QString::fromLatin1(text); // a link-local one with its %interface
            if (!out.contains(address)) out << address;
        }
        freeaddrinfo(found);
        return out;
    };
    l.route = [](const QString& address) {
        QStringList args{"-n", "get"};
        if (address.contains(':')) args << "-inet6";
        return quietly("route", args << address).text();
    };
    l.interfaceInfo = [](const QString& interface) { return quietly("ifconfig", {"-v", interface}).text(); };
    return l;
}

Verdict check(const QString& alias, const Lookups& l) {
    Verdict v;
    const auto unknown = [&v](const QString& why) {
        v.kind = Kind::Unknown;
        v.why = why;
        return v;
    };
    // ssh's own reading of the alias: its HostName, numeric ones in their
    // canonical form (a link-local address keeps its %interface only while
    // the Mac has that interface).
    const QString g = l.sshConfig(alias);
    const QString host = value(g, "hostname");
    if (host.isEmpty()) return unknown("ssh -G " + alias + " names no host");
    // What ssh does past the routing table, the Mac can't see.
    if (isSet(value(g, "proxyjump")))
        return unknown("it goes through ProxyJump " + value(g, "proxyjump") + ", past which this Mac does not see");
    if (isSet(value(g, "proxycommand"))) return unknown("it goes through a ProxyCommand, past which this Mac does not see");
    if (isSet(value(g, "bindinterface"))) return unknown("ssh binds it to BindInterface " + value(g, "bindinterface"));
    if (isSet(value(g, "bindaddress"))) return unknown("ssh binds it to BindAddress " + value(g, "bindaddress"));

    const QStringList addresses = l.resolve(host, value(g, "addressfamily"));
    if (addresses.isEmpty()) return unknown(host + " resolves to no address");
    for (const QString& a : addresses) {
        Hop h;
        h.address = a;
        h.interface = routeInterface(l.route(a));
        if (!h.interface.isEmpty())
            h.kind = kindOf(h.interface, l, &h.what);
        else if (a.startsWith("fe80:", Qt::CaseInsensitive) && !a.contains('%'))
            h.what = "link-local, on an interface the Mac does not have now: the dock unplugged?";
        else
            h.what = "no route to it";
        v.hops << h;
    }

    const auto all = [&v](std::initializer_list<Kind> kinds) {
        return std::all_of(v.hops.begin(), v.hops.end(),
                           [&](const Hop& h) { return std::find(kinds.begin(), kinds.end(), h.kind) != kinds.end(); });
    };
    // Every address ssh may take counts: it takes the first that answers.
    if (all({Kind::Loopback})) v.kind = Kind::Loopback;
    else if (all({Kind::Wired, Kind::Loopback})) v.kind = Kind::Wired;
    else if (all({Kind::WiFi})) v.kind = Kind::WiFi;
    else if (all({Kind::WiFi, Kind::Wired, Kind::Loopback}))
        return unknown(host + " is " + describe(v.hops, {Kind::WiFi}) + ", and " +
                       describe(v.hops, {Kind::Wired, Kind::Loopback}) + ": ssh takes whichever answers first");
    else
        return unknown(QString(v.hops.size() == 1 ? "its address is " : "of its addresses, ") +
                       describe(v.hops, {Kind::Unknown}));
    return v;
}

QString refusal(const QString& alias, const QString& verb, const Verdict& v, const Lookups& l) {
    QString t = v.kind == Kind::WiFi
                    ? alias + " is over Wi-Fi from this Mac: " + describe(v.hops, {Kind::WiFi}) + ".\n"
                    : "cannot tell whether " + alias + " is over a cable from this Mac: " + v.why + ".\n";
    t += "  " + tool(verb) + " sends to a real deck only over a cable.\n";
    // Its cable's alias, ALIAS-eth (deck alias): one ssh -G knows names a
    // host of its own rather than itself.
    if (alias.endsWith("-eth")) {
        t += "  Over the cable: the Mac cabled to the deck's switch; if its interface changed, " +
             tool("deck alias --host " + alias.chopped(4)) + " writes " + alias + " again\n";
    } else {
        const QString eth = alias + "-eth";
        const bool known = value(l.sshConfig(eth), "hostname").compare(eth, Qt::CaseInsensitive) != 0;
        t += known ? "  Over the cable: --host " + eth + "\n"
                   : "  Over the cable: --host " + eth + ", once " + tool("deck alias --host " + alias) +
                         " has made it (the deck cabled to the Mac's switch)\n";
    }
    t += v.kind == Kind::WiFi ? "  Over Wi-Fi all the same: --wifi" : "  Through it all the same: --wifi";
    return t;
}

void require(Deck& d, const QString& verb, bool wifi, const Lookups& l) {
    if (d.emulated() || wifi) return;
    const Verdict v = check(d.name(), l);
    if (v.kind != Kind::Wired && v.kind != Kind::Loopback) fail(refusal(d.name(), verb, v, l));
    QStringList through;
    for (const Hop& h : v.hops)
        if (!through.contains(h.interface + " (" + h.what + ")")) through << h.interface + " (" + h.what + ")";
    print() << d.name() << (v.kind == Kind::Loopback ? ": this Mac itself, " : ": over a cable, the Mac's ")
            << through.join(", ") << "\n";
}

} // namespace cable
