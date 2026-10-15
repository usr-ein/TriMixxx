// decks/cable: a real deck takes deploys, updates and prepares over a cable.
// How the Mac reaches it, from what ssh, the resolver, the routing table and
// ifconfig say -- faked here -- and what deploy, ship and prepare then do.

#include "decks/cable.h"
#include "decks/deck.h"
#include "util/fail.h"

#include <QHash>
#include <QRegularExpression>
#include <QTest>

namespace {

// ifconfig -v's own lines, trimmed (an MBP's, 2026-10-15).
const QString kWifi = "en0: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500 index 14\n"
                      "\teflags=442008d0<ADV_REPORT,ACCEPT_RTADV,TXSTART,ARPLL,NOACKPRI,CHANNEL_DRV,FASTLN_ON>\n"
                      "\tinet 192.168.1.20 netmask 0xffffff00 broadcast 192.168.1.255\n"
                      "\tstatus: active\n"
                      "\ttype: Wi-Fi\n"
                      "\tagent domain:Skywalk type:NetIf flags:0xa443 desc:\"Userspace Networking\"\n"
                      "\tlink quality: 100 (good)\n";
const QString kDock = "en12: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500 index 25\n"
                      "\tinet 169.254.10.20 netmask 0xffff0000 broadcast 169.254.255.255\n"
                      "\tmedia: autoselect (1000baseT <full-duplex>)\n"
                      "\tstatus: active\n"
                      "\ttype: USB Ethernet\n";
const QString kBuiltIn = "en7: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500 index 9\n"
                         "\tstatus: active\n\ttype: Ethernet\n";
const QString kLoopback = "lo0: flags=8049<UP,LOOPBACK,RUNNING,MULTICAST> mtu 16384 index 1\n"
                          "\teflags=10000000<SENDLIST>\n\tinet 127.0.0.1 netmask 0xff000000\n";
const QString kThunderbolt = "en1: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500 index 10\n"
                             "\tstatus: active\n\ttype: IP over Thunderbolt\n";
const QString kTunnel = "utun3: flags=8051<UP,POINTOPOINT,RUNNING,MULTICAST> mtu 1380 index 21\n"
                        "\tinet 100.64.0.7 --> 100.64.0.7 netmask 0xffffffff\n\tlink quality: -1 (unknown)\n";
const QString kThunderboltBridge = "bridge0: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500 index 15\n"
                                   "\tConfiguration:\n"
                                   "\t\tid 0:0:0:0:0:0 priority 0 hellotime 0 fwddelay 0\n"
                                   "\tmember: en1 flags=3<LEARNING,DISCOVER>\n"
                                   "\t        ifmaxaddr 0 port 10 priority 0 path cost 0\n"
                                   "\tmember: en2 flags=3<LEARNING,DISCOVER>\n"
                                   "\tstatus: inactive\n";
const QString kSharingBridge = "bridge100: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500 index 30\n"
                               "\tmember: en12 flags=3<LEARNING,DISCOVER>\n\tstatus: active\n";

// What the Mac says, as the test sets it out; every lookup counted.
struct Mac {
    QHash<QString, QString> sshG;     // alias -> extra `ssh -G` lines; hostname defaults to the alias
    QHash<QString, QStringList> dns;  // host -> its addresses; a numeric one is itself
    QHash<QString, QString> routes;   // address -> interface; else "not in table"
    QHash<QString, QString> ifconfig; // interface -> ifconfig -v
    QStringList families;             // what resolve() was asked, by AddressFamily
    int calls = 0;

    cable::Lookups lookups() {
        cable::Lookups l;
        l.sshConfig = [this](const QString& alias) {
            calls++;
            const QString own = sshG.value(alias);
            return (own.contains("hostname ") ? QString() : "hostname " + alias.toLower() + "\n") + own +
                   (own.contains("addressfamily ") ? QString() : "addressfamily any\n") + "port 22\nuser sam1902\n";
        };
        l.resolve = [this](const QString& host, const QString& family) {
            calls++;
            families << family;
            if (dns.contains(host)) return dns.value(host);
            return host.contains(QRegularExpression("^[0-9.]+$|:")) ? QStringList{host} : QStringList{};
        };
        l.route = [this](const QString& address) {
            calls++;
            if (!routes.contains(address)) return QString("route: writing to routing socket: not in table");
            return "   route to: " + address + "\ndestination: default\n  interface: " + routes.value(address) +
                   "\n      flags: <UP,DONE,CLONING>\n";
        };
        l.interfaceInfo = [this](const QString& interface) {
            calls++;
            return ifconfig.value(interface);
        };
        return l;
    }
};

// The Mac of 2026-10-15: Wi-Fi, the dock's cable, trimixxx-pi over Wi-Fi and
// its -eth alias over the dock.
Mac sams() {
    Mac m;
    m.ifconfig = {{"en0", kWifi}, {"en12", kDock}, {"en7", kBuiltIn}, {"lo0", kLoopback}, {"en1", kThunderbolt},
                  {"en2", kThunderbolt}, {"utun3", kTunnel}, {"bridge0", kThunderboltBridge}, {"bridge100", kSharingBridge}};
    m.sshG = {{"trimixxx-pi", "hostname 192.168.1.50\n"},
              {"trimixxx-pi-eth", "hostname fe80::1:2ff:fe03:401%en12\nhostkeyalias 192.168.1.50\n"}};
    m.routes = {{"192.168.1.50", "en0"}, {"fe80::1:2ff:fe03:401%en12", "en12"}, {"127.0.0.1", "lo0"}};
    return m;
}

class FakeDeck : public Deck {
public:
    FakeDeck(const QString& name, bool emulated) : m_ssh{name, {}}, m_emulated(emulated) {}
    QString name() const override { return m_ssh.alias; }
    bool    emulated() const override { return m_emulated; }
    const SshTarget& ssh() const override { return m_ssh; }
    void    requireUp() override {}
    QString wrappers() override { return {}; }
    QString controls(const QStringList&) override { return {}; }
    void    shot(const QString&) override {}

private:
    SshTarget m_ssh;
    bool      m_emulated;
};

} // namespace

class TestCable : public QObject {
    Q_OBJECT

private slots:
    // ---- the parts ----
    void routeInterface() {
        QCOMPARE(cable::routeInterface("   route to: 192.168.1.1\ndestination: 192.168.1.1\n  interface: en0\n"
                                       "      flags: <UP,HOST,DONE,LLINFO,WASCLONED,IFSCOPE,IFREF,ROUTER>\n"),
                 QString("en0"));
        QCOMPARE(cable::routeInterface("   route to: fe80::1%en12\ndestination: fe80::%en12\n"
                                       "       mask: ffff:ffff:ffff:ffff::\n  interface: en12\n"),
                 QString("en12"));
        QCOMPARE(cable::routeInterface("route: writing to routing socket: not in table"), QString());
    }
    // The kernel's kind of interface, as ifconfig -v names it.
    void interfaceKinds() {
        QString what;
        QCOMPARE(cable::interfaceKind(kWifi, &what), cable::Kind::WiFi);
        QCOMPARE(what, QString("Wi-Fi"));
        QCOMPARE(cable::interfaceKind(kDock, &what), cable::Kind::Wired);
        QCOMPARE(what, QString("USB Ethernet"));
        QCOMPARE(cable::interfaceKind(kBuiltIn, &what), cable::Kind::Wired);
        QCOMPARE(what, QString("Ethernet"));
        QCOMPARE(cable::interfaceKind(kLoopback, &what), cable::Kind::Loopback);
        QCOMPARE(cable::interfaceKind(kThunderbolt, &what), cable::Kind::Unknown);
        QVERIFY(what.contains("IP over Thunderbolt"));
        QCOMPARE(cable::interfaceKind(kTunnel, &what), cable::Kind::Unknown);
        QVERIFY(what.contains("no kind"));
        QCOMPARE(cable::interfaceKind({}, &what), cable::Kind::Unknown);
        QCOMPARE(what, QString("not there"));
        // A bridge: unknown by itself, its members named.
        QStringList members;
        QCOMPARE(cable::interfaceKind(kThunderboltBridge, &what, &members), cable::Kind::Unknown);
        QCOMPARE(members, QStringList({"en1", "en2"}));
    }

    // ---- how the Mac reaches an alias ----
    void wifi() {
        Mac m = sams();
        const cable::Verdict v = cable::check("trimixxx-pi", m.lookups());
        QCOMPARE(v.kind, cable::Kind::WiFi);
        QCOMPARE(v.hops.size(), 1);
        QCOMPARE(v.hops[0].address, QString("192.168.1.50"));
        QCOMPARE(v.hops[0].interface, QString("en0"));
    }
    void theDocksCable() {
        Mac m = sams();
        const cable::Verdict v = cable::check("trimixxx-pi-eth", m.lookups());
        QCOMPARE(v.kind, cable::Kind::Wired);
        QCOMPARE(v.hops[0].interface, QString("en12"));
        QCOMPARE(v.hops[0].what, QString("USB Ethernet"));
    }
    void builtInEthernet() {
        Mac m = sams();
        m.sshG["deck"] = "hostname 10.0.0.5\n";
        m.routes["10.0.0.5"] = "en7";
        QCOMPARE(cable::check("deck", m.lookups()).kind, cable::Kind::Wired);
    }
    // The Mac itself: an emulated deck's port, by an alias.
    void loopback() {
        Mac m = sams();
        m.sshG["trimixxx0"] = "hostname 127.0.0.1\nport 2222\n";
        QCOMPARE(cable::check("trimixxx0", m.lookups()).kind, cable::Kind::Loopback);
    }
    // Internet Sharing's bridge over the dock is a cable; the Thunderbolt Bridge isn't.
    void bridges() {
        Mac m = sams();
        m.sshG["shared"] = "hostname 192.168.2.10\n";
        m.routes["192.168.2.10"] = "bridge100";
        const cable::Verdict shared = cable::check("shared", m.lookups());
        QCOMPARE(shared.kind, cable::Kind::Wired);
        QVERIFY(shared.hops[0].what.contains("en12"));
        m.routes["192.168.2.10"] = "bridge0";
        const cable::Verdict tb = cable::check("shared", m.lookups());
        QCOMPARE(tb.kind, cable::Kind::Unknown);
        QVERIFY(tb.why.contains("en1"));
    }
    void interfacesOfNoKnownKind() {
        Mac m = sams();
        m.sshG["vpn"] = "hostname 100.64.0.9\n";
        m.routes["100.64.0.9"] = "utun3";
        const cable::Verdict vpn = cable::check("vpn", m.lookups());
        QCOMPARE(vpn.kind, cable::Kind::Unknown);
        QVERIFY(vpn.why.contains("utun3"));
        m.routes["100.64.0.9"] = "en1";
        QCOMPARE(cable::check("vpn", m.lookups()).kind, cable::Kind::Unknown);
    }
    // The dock unplugged: ssh -G drops the %en12 of an interface the Mac lacks.
    void dockUnplugged() {
        Mac m = sams();
        m.sshG["trimixxx-pi-eth"] = "hostname fe80::1:2ff:fe03:401\n";
        const cable::Verdict v = cable::check("trimixxx-pi-eth", m.lookups());
        QCOMPARE(v.kind, cable::Kind::Unknown);
        QVERIFY(v.why.contains("link-local"));
        QVERIFY(v.why.contains("dock"));
    }
    void noRoute() {
        Mac m = sams();
        m.sshG["far"] = "hostname 10.9.9.9\n";
        const cable::Verdict v = cable::check("far", m.lookups());
        QCOMPARE(v.kind, cable::Kind::Unknown);
        QVERIFY(v.why.contains("no route"));
    }
    void noAddress() {
        Mac m = sams();
        m.sshG["trimixxx-pi-2"] = "hostname trimixxx2.local\n";
        const cable::Verdict v = cable::check("trimixxx-pi-2", m.lookups());
        QCOMPARE(v.kind, cable::Kind::Unknown);
        QVERIFY(v.why.contains("trimixxx2.local"));
    }
    // A name: every address ssh may take counts. All over Wi-Fi is Wi-Fi; both
    // ways is not told: ssh takes whichever answers first.
    void aNamesAddresses() {
        Mac m = sams();
        m.sshG["trimixxx-pi-2"] = "hostname trimixxx2.local\n";
        m.dns["trimixxx2.local"] = {"fe80::5:6ff:fe07:801%en0", "192.168.1.51"};
        m.routes["fe80::5:6ff:fe07:801%en0"] = "en0";
        m.routes["192.168.1.51"] = "en0";
        QCOMPARE(cable::check("trimixxx-pi-2", m.lookups()).kind, cable::Kind::WiFi);
        m.dns["trimixxx2.local"] = {"fe80::5:6ff:fe07:801%en12", "192.168.1.51"};
        m.routes["fe80::5:6ff:fe07:801%en12"] = "en12";
        const cable::Verdict both = cable::check("trimixxx-pi-2", m.lookups());
        QCOMPARE(both.kind, cable::Kind::Unknown);
        QVERIFY(both.why.contains("192.168.1.51 through en0 (Wi-Fi)"));
        QVERIFY(both.why.contains("%en12 through en12 (USB Ethernet)"));
        QVERIFY(both.why.contains("whichever answers first"));
    }
    // What ssh does past the routing table, the Mac can't see.
    void proxiesAndBinds() {
        Mac m = sams();
        m.sshG["jumped"] = "hostname 192.168.1.50\nproxyjump gateway\n";
        m.sshG["piped"] = "hostname 192.168.1.50\nproxycommand nc %h %p\n";
        m.sshG["bound"] = "hostname 192.168.1.50\nbindinterface en12\n";
        for (const char* alias : {"jumped", "piped", "bound"}) {
            const cable::Verdict v = cable::check(alias, m.lookups());
            QCOMPARE(v.kind, cable::Kind::Unknown);
            QVERIFY(v.hops.isEmpty()); // decided on ssh -G alone
        }
        QVERIFY(cable::check("jumped", m.lookups()).why.contains("ProxyJump gateway"));
    }
    void resolvedAsSshWould() {
        Mac m = sams();
        m.sshG["v6"] = "hostname deck.local\naddressfamily inet6\n";
        cable::check("v6", m.lookups());
        QCOMPARE(m.families, QStringList({"inet6"}));
    }

    // ---- the refusal ----
    void refusedOverWifi() {
        Mac m = sams();
        const cable::Verdict v = cable::check("trimixxx-pi", m.lookups());
        const QString t = cable::refusal("trimixxx-pi", "deck ship", v, m.lookups());
        QVERIFY(t.startsWith("trimixxx-pi is over Wi-Fi from this Mac: 192.168.1.50 through en0 (Wi-Fi)."));
        QVERIFY(t.contains("pi-qemu deck ship sends to a real deck only over a cable"));
        QVERIFY(t.contains("Over the cable: --host trimixxx-pi-eth\n")); // ssh knows it
        QVERIFY(t.endsWith("Over Wi-Fi all the same: --wifi"));
    }
    void refusedWithoutAnEthAlias() {
        Mac m = sams();
        m.sshG["trimixxx-pi-2"] = "hostname 192.168.1.51\n";
        m.routes["192.168.1.51"] = "en0";
        const QString t = cable::refusal("trimixxx-pi-2", "deck deploy", cable::check("trimixxx-pi-2", m.lookups()), m.lookups());
        QVERIFY(t.contains("--host trimixxx-pi-2-eth, once pi-qemu deck alias --host trimixxx-pi-2 has made it"));
    }
    void refusedUntold() {
        Mac m = sams();
        m.sshG["trimixxx-pi-eth"] = "hostname fe80::1:2ff:fe03:401\n";
        const QString t = cable::refusal("trimixxx-pi-eth", "deck prepare", cable::check("trimixxx-pi-eth", m.lookups()), m.lookups());
        QVERIFY(t.startsWith("cannot tell whether trimixxx-pi-eth is over a cable from this Mac: "));
        QVERIFY(t.contains("pi-qemu deck alias --host trimixxx-pi writes trimixxx-pi-eth again"));
        QVERIFY(t.endsWith("Through it all the same: --wifi"));
    }

    // ---- deploy, ship and prepare ----
    void emulatedDecksAreNeverGated() {
        Mac m = sams();
        FakeDeck d("feat", true);
        cable::require(d, "deck deploy", false, m.lookups());
        QCOMPARE(m.calls, 0);
    }
    void wifiFlagGoesAsToday() {
        Mac m = sams();
        FakeDeck d("trimixxx-pi", false);
        cable::require(d, "deck ship", true, m.lookups());
        QCOMPARE(m.calls, 0);
    }
    void refusedBeforeAnything() {
        Mac m = sams();
        FakeDeck d("trimixxx-pi", false);
        try {
            cable::require(d, "deck deploy", false, m.lookups());
            QFAIL("no failure");
        } catch (const Failure& f) {
            QCOMPARE(f.status, 1);
            QVERIFY(f.message.contains("over Wi-Fi"));
            QVERIFY(f.message.contains("--wifi"));
        }
    }
    void throughTheCable() {
        Mac m = sams();
        FakeDeck d("trimixxx-pi-eth", false);
        cable::require(d, "deck deploy", false, m.lookups()); // no failure
        FakeDeck local("trimixxx0", false);
        m.sshG["trimixxx0"] = "hostname 127.0.0.1\nport 2222\n";
        cable::require(local, "deck deploy", false, m.lookups());
    }
};

QTEST_GUILESS_MAIN(TestCable)
#include "tst_cable.moc"
