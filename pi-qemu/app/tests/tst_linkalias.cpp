// decks/linkalias: a real deck's Ethernet as an ssh alias of its own -- the
// parts that need no deck: what the deck and the Mac say, and the config.

#include "decks/linkalias.h"

#include <QTest>

class TestLinkAlias : public QObject {
    Q_OBJECT

private slots:
    // trimixxx1's, 2026-10-10.
    void deckAddress() {
        QCOMPARE(linkalias::linkLocal("2: eth0    inet6 fe80::dea6:32ff:fe88:2118/64 scope link noprefixroute \\"
                                      "       valid_lft forever preferred_lft forever"),
                 QString("fe80::dea6:32ff:fe88:2118"));
        QCOMPARE(linkalias::linkLocal("1\n"), QString());
    }
    // The Mac's wired devices: never Wi-Fi, never Thunderbolt (a bridge, or IP over the port).
    void wiredDevices() {
        const QString ports = "\nHardware Port: Ethernet Adapter (en4)\nDevice: en4\nEthernet Address: 0a:00\n\n"
                              "Hardware Port: Dell Universal Dock D6000\nDevice: en12\nEthernet Address: 0b:00\n\n"
                              "Hardware Port: Thunderbolt Bridge\nDevice: bridge0\nEthernet Address: N/A\n\n"
                              "Hardware Port: Wi-Fi\nDevice: en0\nEthernet Address: 0c:00\n\n"
                              "Hardware Port: Thunderbolt 1\nDevice: en1\nEthernet Address: 0d:00\n\n"
                              "VLAN Configurations\n===================\n";
        QCOMPARE(linkalias::wiredDevices(ports), QStringList({"en4", "en12"}));
    }
    // From `ssh -G trimixxx-pi`: its host key's name, and what the new alias keeps.
    void fromTheUsualAlias() {
        const QString sshG = "user sam1902\nhostname 192.168.1.80\nport 22\nidentitiesonly no\n"
                             "stricthostkeychecking ask\nidentityfile ~/.ssh/with_pass/rsa_sam\n"
                             "userknownhostsfile /Users/sam1902/.ssh/known_hosts\nforwardagent yes\nconnecttimeout 60\n";
        QCOMPARE(linkalias::hostKeyAlias(sshG), QString("192.168.1.80"));
        QCOMPARE(linkalias::keptOptions(sshG), QStringList({"User sam1902", "IdentityFile ~/.ssh/with_pass/rsa_sam",
                                                            "ForwardAgent yes", "ConnectTimeout 60"}));
        QCOMPARE(linkalias::hostKeyAlias("hostname deck.local\nport 2222\n"), QString("[deck.local]:2222"));
        QCOMPARE(linkalias::hostKeyAlias("hostname x\nhostkeyalias known-as\n"), QString("known-as"));
        QCOMPARE(linkalias::keptOptions("port 2222\nconnecttimeout none\nforwardagent no\n"), QStringList({"Port 2222"}));
    }
    // Only its own marked block changes: a host added beside the others, a host
    // again replaced where it was, the same twice the same file.
    void configBlock() {
        const QString sams = "Host github.com\n    User git\n\nHost trimixxx-pi\n    HostName 192.168.1.80\n\n\n";
        const QString begin = "# >>> pi-qemu deck alias: real decks over their other links (it rewrites this block) >>>\n";
        const QString end = "# <<< pi-qemu deck alias <<<\n";
        const QString one = linkalias::withHost(sams, "trimixxx-pi-eth", {"HostName fe80::1%%en12", "User sam1902"});
        QCOMPARE(one, "Host github.com\n    User git\n\nHost trimixxx-pi\n    HostName 192.168.1.80\n\n" + begin +
                          "Host trimixxx-pi-eth\n    HostName fe80::1%%en12\n    User sam1902\n" + end);
        const QString two = linkalias::withHost(one, "trimixxx-pi-2-eth", {"HostName fe80::2%%en12"});
        const QString again = linkalias::withHost(two, "trimixxx-pi-eth", {"HostName fe80::3%%en13"});
        QCOMPARE(again, "Host github.com\n    User git\n\nHost trimixxx-pi\n    HostName 192.168.1.80\n\n" + begin +
                            "Host trimixxx-pi-eth\n    HostName fe80::3%%en13\n\n"
                            "Host trimixxx-pi-2-eth\n    HostName fe80::2%%en12\n" + end);
        QCOMPARE(linkalias::withHost(again, "trimixxx-pi-eth", {"HostName fe80::3%%en13"}), again);
        QCOMPARE(linkalias::withHost({}, "a-eth", {"HostName fe80::4%%en5"}), begin + "Host a-eth\n    HostName fe80::4%%en5\n" + end);
        // What follows the block, if someone wrote there, stays.
        QVERIFY(linkalias::withHost(one + "Host later\n", "trimixxx-pi-eth", {"HostName fe80::1%%en12"}).endsWith(end + "Host later\n"));
    }
};

QTEST_GUILESS_MAIN(TestLinkAlias)
#include "tst_linkalias.moc"
