#pragma once
// A link: the network the decks' CDJ ports share, as the players in a booth
// share one switch. Emulated decks started on the same link see each other
// over Pro DJ Link and load each other's tracks; a deck on no link has its
// eth0 on a cable to an empty switch.
//
// A link is a directory, ~/.pi-qemu/links/NET/, holding one Unix datagram
// socket per board on it, MEMBER.sock. QEMU's GENET (the Pi's eth0) takes one
// end of a socketpair as its network (-netdev dgram,local.type=fd); a thread
// of its own here reads the other end, and puts each frame the Pi sends on the
// other members' sockets, and each frame they send on the Pi's wire. Like a
// switch, it learns which member has which MAC and sends a frame meant for one
// of them to that one alone; broadcasts, multicasts and frames for a MAC not
// heard from yet go to every member. A deck never hears its own frames back.
// Nothing leaves the Mac: the sockets are files, not ports.
//
// A member whose name starts with a dot only listens (a capture: link
// capture, link devices) and gets every frame on the link.

#include <QList>
#include <QString>
#include <QStringList>

#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace links {

QString root();                       // $PI_QEMU_LINKS, or ~/.pi-qemu/links
QString dir(const QString& net);      // root()/NET
bool    validName(const QString& s);  // a link's or a member's: letters, digits, - and _
QString macFor(const QString& deck);  // a deck's own eth0 MAC on a link, from its name
QStringList nets();                   // the links that have, or had, members

struct Member {
    QString name;
    bool    live = false; // a board holds its socket (else a crashed one left it)
    bool    listener = false;
};
QList<Member> members(const QString& net);
void forget(const QString& net, const QString& member); // its socket, if nothing holds it

// What a Pro DJ Link keep-alive (UDP 50000, type 0x06) in an Ethernet frame
// says about its sender; number 0 if the frame is not one.
struct KeepAlive {
    int      number = 0;
    QString  name, ip, mac;
};
KeepAlive keepAlive(const char* frame, size_t size);

} // namespace links

class Link {
public:
    Link(QString net, QString member);
    ~Link(); // stops it, and takes its socket off the link
    Link(const Link&) = delete;
    Link& operator=(const Link&) = delete;

    bool open(QString* error);  // the member's socket, and the thread
    void close();

    // QEMU's end: inherited by QEMU, as -netdev dgram,local.type=fd,local.str=N.
    // It outlives each QEMU, which a guest reboot replaces.
    int  qemuFd() const { return m_qemu; }
    // Frames that reached the Pi's end while no QEMU read it: a Pi that was
    // off heard nothing, so they go before the next QEMU starts.
    void drain();

    // Unplugged, frames go nowhere either way, though the Pi still sees a
    // link: what a restored deck waits in until it has its own identity.
    void setPlugged(bool plugged);
    bool plugged() const { return m_plugged; }

    QString net() const { return m_net; }
    QString member() const { return m_member; }
    QString path() const;

    struct Heard { // what this board's own keep-alives say, since it was plugged in
        int     number = 0;
        QString ip, mac;
    };
    Heard heard() const;
    quint64 framesOut() const { return m_out; }
    quint64 framesIn() const { return m_in; }
    quint64 dropped() const { return m_dropped; }

private:
    using Mac = std::array<unsigned char, 6>;
    void run();
    void refreshMembers();
    void fromPi(const char* frame, size_t n);
    void fromMember(const char* frame, size_t n, const std::string& from);
    bool sendTo(const std::string& path, const char* frame, size_t n);

    QString m_net, m_member;
    int m_qemu = -1, m_pi = -1, m_sock = -1;
    std::atomic<bool> m_stop{false}, m_plugged{true};
    std::atomic<quint64> m_out{0}, m_in{0}, m_dropped{0};
    std::thread m_thread;

    // The thread's own.
    std::vector<std::string> m_members, m_listeners; // the other members' socket paths
    std::map<Mac, std::string> m_where;              // a MAC, and the member it was heard from
    qint64 m_scanned = 0;

    mutable std::mutex m_heardLock;
    Heard m_heard;
};

// A member that only listens, for as long as it lives: every frame on the link.
class LinkTap {
public:
    explicit LinkTap(QString net);
    ~LinkTap();
    LinkTap(const LinkTap&) = delete;
    LinkTap& operator=(const LinkTap&) = delete;
    bool open(QString* error);
    // Waits up to timeoutMs for a frame; false on timeout. `from` names the
    // member that sent it.
    bool next(std::vector<char>* frame, QString* from, int timeoutMs);

private:
    QString m_net, m_path;
    int m_sock = -1;
};
