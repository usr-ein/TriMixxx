#pragma once
// The emulated Raspberry Pi 4: the firmware step, then the patched QEMU, and
// QMP to drive it (power, USB hot-plug). A guest reboot pauses QEMU (-action
// reboot=shutdown,shutdown=pause): pi-qemu reads what the firmware would keep
// across it -- the reboot flags (tryboot) and the partition Linux asked for --
// stops QEMU, and starts the board again from the firmware step with them. So
// a changed card (cmdline.txt, config.txt, autoboot.txt) is read exactly as on
// a Pi, and an A/B trial starts as on one. A power-on from the panel or the CLI
// forgets both, as a power cycle does.

#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QQueue>

#include <atomic>
#include <functional>
#include <sys/types.h>

class Link;

// QEMU's pid while it runs: for a signal handler that must stop it before
// pi-qemu goes (commands/run.cpp). It is also in <run dir>/qemu.pid, for
// whoever has to stop a pi-qemu that was killed outright (deck kill).
extern std::atomic<pid_t> g_qemuPid;

struct MachineOptions {
    QString card, runDir, qemu, dtmerge;
    QString accel = "hvf";      // tcg: a real Cortex-A72 model, slow
    QString display = "window"; // window | vnc | none
    QString audio = "none";     // none | speakers | wav:FILE -- none is silent
    QString mac = "02:54:4d:58:00:00";
    QString net = "user";       // user | restricted | none
    QString serialLog;          // the S3's UART to a file instead (debugging)
    int     fbWidth = 1280, fbHeight = 800, fbDepth = 16;
    int     sshPort = 2222;
    QStringList sticks;         // image files plugged in at power-on
    QString restore;            // a saved machine (pi-qemu save) to start from, once
    QString name;               // the board's: QEMU's window title
    Link*   link = nullptr;     // eth0 on a link (board/link.h), else on an empty switch
};

class Machine : public QObject {
    Q_OBJECT
public:
    explicit Machine(MachineOptions o, QObject* parent = nullptr);
    ~Machine() override;

    bool powerOn(QString* error);
    void pullPlug();               // like the mains switch: no shutdown
    bool running() const { return m_proc.state() != QProcess::NotRunning; }
    const MachineOptions& options() const { return m_o; }
    QString s3Socket() const;
    QString runDir() const { return m_o.runDir; }

    using Reply = std::function<void(const QJsonObject& reply)>;
    void qmp(const QString& command, const QJsonObject& args = {}, Reply done = {});

    // Pause the Pi, write the whole machine to `file`, and stop QEMU: the Pi
    // is then off, and its card and `file` belong together (run --restore).
    using Done = std::function<void(bool ok, const QString& message)>;
    void save(const QString& file, Done done);

signals:
    void status(const QString& line); // human-readable, for the window and the terminal
    void poweredOff();

private:
    void onFinished();
    void onGuestStopped();
    void connectQmp();
    void onQmpBytes();
    void finishRestore(int tries);
    void pollMigration(int tries, Done done);

    MachineOptions m_o;
    QProcess       m_proc;
    QLocalSocket   m_qmp;
    QByteArray     m_qmpBuf;
    QQueue<Reply>  m_pending;
    QString        m_shutdownReason;
    bool           m_nextTryboot = false;  // for the power-on after a guest reset
    int            m_nextPartition = 0;
    bool           m_qmpReady = false;
    bool           m_stopping = false;
    bool           m_restoring = false;  // QEMU is loading m_o.restore
    bool           m_restoreUsed = false; // only the first power-on restores
    QString        m_savedTo;            // QEMU stopped after saving the machine here
    qint64         m_startMs = 0;
};
