#pragma once
// A real deck takes deploys, updates and prepares over a cable: `deck
// deploy`, `deck ship` and `deck prepare` with --host check how the Mac
// reaches it before they send anything, and refuse Wi-Fi unless --wifi.
//
// The route is found as ssh takes it: the alias's HostName and options from
// `ssh -G` (ssh's own reading of ~/.ssh/config), its addresses as ssh
// resolves them, the Mac's interface to each from `route -n get`, and that
// interface's kind from the kernel (`ifconfig -v`: type Wi-Fi, Ethernet, USB
// Ethernet). A route it cannot tell is refused too, saying why: a proxy, an
// interface of no known kind, a name whose addresses go both ways. The Mac
// itself (loopback) passes: an emulated deck's port, or a tunnel of one's own.

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class Deck;

namespace cable {

enum class Kind { Wired, WiFi, Loopback, Unknown };

// One address the alias reaches the deck at, and how the Mac gets there.
struct Hop {
    QString address;   // as ssh connects to it: "192.168.1.80", "fe80::1%en12"
    QString interface; // the Mac's: "en12"; empty without a route
    Kind    kind = Kind::Unknown;
    QString what;      // the interface's kind in words ("USB Ethernet"), or why it is unknown
};

struct Verdict {
    Kind    kind = Kind::Unknown; // the route's, all its hops agreeing; Unknown otherwise
    QString why;                  // Unknown: why, in words
    QVector<Hop> hops;
};

// What the Mac says, each from its own command; tests fake them.
struct Lookups {
    std::function<QString(const QString& alias)> sshConfig;                         // ssh -G ALIAS
    std::function<QStringList(const QString& host, const QString& family)> resolve; // getaddrinfo, as ssh calls it
    std::function<QString(const QString& address)> route;                           // route -n get ADDRESS
    std::function<QString(const QString& interface)> interfaceInfo;                 // ifconfig -v INTERFACE
};
Lookups system();

// How the Mac reaches `alias`.
Verdict check(const QString& alias, const Lookups& l);

// For `verb` ("deck deploy"): fails with the refusal, before anything is
// sent, unless `d` is emulated, `wifi` is set, or the route is a cable (or
// the Mac itself); then says which interface it takes.
void require(Deck& d, const QString& verb, bool wifi, const Lookups& l = system());
// The refusal's text, for `verdict` on `alias` (what require fails with).
QString refusal(const QString& alias, const QString& verb, const Verdict& verdict, const Lookups& l);

// ---- the parts, apart (tests/tst_cable) ----

// The interface `route -n get` names; empty if none.
QString routeInterface(const QString& routeGet);
// An interface's kind from `ifconfig -v`, in words into `what`; a bridge's
// members' named into `members` (it is wired when they all are).
Kind interfaceKind(const QString& ifconfigV, QString* what, QStringList* members = nullptr);

} // namespace cable
