#include "pipeline/golden.h"

#include "pipeline/imagebuild.h"
#include "util/fail.h"
#include "util/files.h"
#include "util/git.h"
#include "util/paths.h"
#include "util/print.h"
#include "util/process.h"
#include "util/tool.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

#include <cerrno>
#include <csignal>

namespace golden {

namespace {

const QString kDeck = "trimixxx0";

QString firstLine(const QByteArray& text) {
    const QString t = QString::fromUtf8(text).trimmed();
    return t.isEmpty() ? QString("no answer") : t.section('\n', 0, 0);
}

// origin/main, fetched first: what the golden is compared with. A fetch that
// fails (no network) leaves origin/main as last fetched, and says so. Empty
// when the main checkout has neither origin/main nor main.
QString fetchMain() {
    const QString repo = paths::mainCheckout();
    proc::Options o;
    o.quiet = true;
    o.timeoutMs = 30000;
    o.env["GIT_TERMINAL_PROMPT"] = "0";
    o.env["GIT_SSH_COMMAND"] = "ssh -o BatchMode=yes -o ConnectTimeout=10";
    const proc::Result f = proc::capture("git", {"-C", repo, "fetch", "--quiet", "origin", "main"}, o);
    QString main = git::tryOut(repo, {"rev-parse", "--verify", "--quiet", "refs/remotes/origin/main"});
    if (main.isEmpty()) main = git::tryOut(repo, {"rev-parse", "--verify", "--quiet", "refs/heads/main"});
    if (!f.ok() && !main.isEmpty())
        print() << "golden: could not fetch origin (" << (f.status == -1 && f.err.isEmpty() ? "timed out" : firstLine(f.err))
                << "): main as last fetched, " << main.left(7) << "\n";
    return main;
}

// A commit of the main checkout, as a record: for its date in what is said.
source::Record commitAt(const QString& sha) {
    source::Record r;
    r.commit = sha;
    r.clean = true;
    const QStringList log = git::tryOut(paths::mainCheckout(), {"log", "-1", "--format=%cI%n%s", sha}).split('\n');
    r.date = log.value(0);
    r.subject = log.value(1);
    return r;
}

QString golden(const source::Record& g) {
    if (!g.empty()) return g.describe();
    return QFileInfo::exists(paths::golden() + ".img") ? QString("a golden with no record (made before pi-qemu kept one)")
                                                        : QString("no golden yet");
}

QString clock(const QString& iso) {
    return QDateTime::fromString(iso, Qt::ISODate).toLocalTime().toString("HH:mm");
}

bool alive(qint64 pid) { return pid > 0 && (::kill(pid_t(pid), 0) == 0 || errno == EPERM); }

QString outFile() { return paths::golden() + ".remake.out"; }

// The golden everyone's decks start from is made by main's pi-qemu (the
// main checkout's build, on PATH), never by a worktree's build of it run by
// its path, which may hold code no one reviewed. A scratch golden
// (PI_QEMU_CACHE) may be made by any.
QString foreignTool() {
    if (!qEnvironmentVariableIsEmpty("PI_QEMU_CACHE")) return {};
    const QString self = QFileInfo(paths::binary()).canonicalFilePath();
    const QString mains = QFileInfo(paths::mainCheckout() + "/pi-qemu/app/build/pi-qemu").canonicalFilePath();
    return self == mains ? QString() : self;
}

// The image build, in the main checkout, a process of its own: it goes on
// if the command that started it is stopped. What it says before its log
// takes over (a refusal, a failure to start) goes to outFile().
qint64 spawn() {
    QDir().mkpath(QFileInfo(outFile()).absolutePath());
    files::write(outFile(), {});
    QProcess p;
    p.setProgram(paths::binary());
    p.setArguments({"image", "build", kDeck, "--no-window"});
    p.setWorkingDirectory(paths::mainCheckout());
    p.setStandardInputFile(QProcess::nullDevice());
    p.setStandardOutputFile(QProcess::nullDevice()); // the log has it all
    p.setStandardErrorFile(outFile());
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("BUILD_WINDOW", "0");
    p.setProcessEnvironment(env);
    // Its own session, and none of this command's files: a caller reading
    // this one's output to its end (crew merge) is not kept waiting by it.
    p.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession |
                               QProcess::UnixProcessFlag::CloseFileDescriptors);
    qint64 pid = 0;
    if (!p.startDetached(&pid)) fail("cannot start " + paths::binary() + ": " + p.errorString());
    return pid;
}

// The step lines ("==> [HH:MM:SS] deploy 004_mixxx") written to `log` since
// `offset`, which moves past them; a log emptied by a new build starts again.
QStringList newSteps(const QString& log, qint64& offset) {
    QFile f(log);
    if (!f.open(QIODevice::ReadOnly)) return {};
    if (f.size() < offset) offset = 0;
    f.seek(offset);
    const QByteArray chunk = f.readAll();
    const qsizetype end = chunk.lastIndexOf('\n');
    if (end < 0) return {};
    offset += end + 1;
    QStringList steps;
    for (const QByteArray& line : chunk.left(end).split('\n'))
        if (line.startsWith("==> [")) steps << QString::fromUtf8(line.mid(4)).trimmed();
    return steps;
}

// Until the build of trimixxx0 lets go of its lock: its steps as they start,
// and a word every five minutes when none does.
void waitFor() {
    const QString log = imagebuild::logFile(kDeck);
    qint64 offset = 0;
    const QStringList before = newSteps(log, offset);
    if (!before.isEmpty()) print() << "golden: " << before.last() << "\n";
    QElapsedTimer quiet, all;
    quiet.start();
    all.start();
    while (imagebuild::running(kDeck)) {
        proc::sleep(2000);
        for (const QString& s : newSteps(log, offset)) {
            print() << "golden: " << s << "\n";
            quiet.restart();
        }
        if (quiet.elapsed() > 300000) {
            print() << "golden: still building (" << all.elapsed() / 60000 << " min of waiting so far)\n";
            quiet.restart();
        }
    }
}

// The last lines of a file, for a failure that left nothing else.
QString tail(const QString& file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QStringList all = QString::fromUtf8(f.readAll()).trimmed().split('\n');
    return all.mid(qMax(0, all.size() - 5)).join("\n  ");
}

// A remake that is over without making the golden main's latest: why, and
// what happens next.
State report(const imagebuild::Attempt& a, const source::Record& main, const source::Record& g) {
    const QString log = imagebuild::logFile(kDeck);
    if (a.state == "built") {
        eprint() << "golden: made again, from " << golden(g) << ", which is not main's latest (" << main.describe()
                 << "): the next check makes it again\n";
        return State::Waiting;
    }
    if (a.state == "failed") {
        eprint() << "golden: main " << main.describe() << " did not build: image build " << kDeck << " failed at "
                 << clock(a.ended) << ": " << a.why << "\n"
                 << "golden: its log: " << log << "\n"
                 << "golden: decks start from the last golden, " << golden(g) << ", NOT main's latest. It is tried "
                 << "again when main moves on, or now: " << tool("image golden --retry") << "\n";
        return State::Failed;
    }
    if (a.state == "unready")
        eprint() << "golden: it could not be made now: " << a.why << ". Decks start from " << golden(g)
                 << ", not main's latest; the next check tries again\n";
    else if (a.state == "interrupted")
        eprint() << "golden: its remake was stopped (^C). Decks start from " << golden(g)
                 << ", not main's latest; the next check starts it again\n";
    else
        eprint() << "golden: its remake ended without a word (" << (a.state.isEmpty() ? QString("no attempt") : a.state)
                 << "; its log: " << log << "). Decks start from " << golden(g) << "; the next check tries again\n";
    return State::Waiting;
}

} // namespace

QString recordFile() { return paths::golden() + ".json"; }

source::Record record() { return source::read(recordFile()); }

Hold::Hold(Mode mode)
    : m_lock(mode == Make ? FileLock(paths::golden() + ".make.lock", FileLock::Exclusive,
                                     "golden: waiting for another command making the golden")
                          : FileLock(paths::golden() + ".lock", mode == Read ? FileLock::Shared : FileLock::Exclusive,
                                     mode == Read ? "golden: waiting while the golden is replaced" : QString())) {}

Outcome ensure(const Options& o) {
    Outcome out;
    const QString repo = paths::mainCheckout();
    out.main = fetchMain();
    out.golden = record();
    if (out.main.isEmpty()) {
        print() << "golden: no main in " << repo << " to compare it with: " << golden(out.golden) << "\n";
        return out;
    }
    const source::Record main = commitAt(out.main);
    qint64 ours = 0; // a remake this started
    int starts = 0;
    bool waited = false; // for a remake someone else started
    QElapsedTimer took;
    for (;;) {
        out.golden = record();
        if (out.golden.known() && source::same(repo, out.golden.commit, out.main)) {
            out.state = State::Current;
            if (ours) print() << "golden: made again in " << took.elapsed() / 60000 << " min: main's latest, " << golden(out.golden) << "\n";
            else if (waited) print() << "golden: made again: main's latest, " << golden(out.golden) << "\n";
            else if (!o.quiet) print() << "golden: main's latest, " << golden(out.golden) << "\n";
            return out;
        }
        if (imagebuild::running(kDeck)) {
            out.state = State::Remaking;
            const imagebuild::Attempt a = imagebuild::attempt(kDeck);
            if (a.pid != ours)
                print() << "golden: " << golden(out.golden) << ", being made again"
                        << (a.state == "building" ? QString(" from %1 since %2 (pid %3)").arg(a.source.describe(), clock(a.started)).arg(a.pid)
                                                  : QString())
                        << "; its log: " << imagebuild::logFile(kDeck) << "\n";
            if (!o.wait) return out;
            if (a.pid != ours) {
                print() << "golden: waiting for it\n";
                waited = true;
            }
            waitFor();
            continue;
        }
        if (ours) {
            const imagebuild::Attempt a = imagebuild::attempt(kDeck);
            if (a.pid == ours) {
                out.state = report(a, main, out.golden);
                return out;
            }
            if (alive(ours) && took.elapsed() < 60000) { // not holding the lock yet
                proc::sleep(250);
                continue;
            }
            // It never built: another took the deck's lock first, it failed
            // before, or it hangs before its build (a minute).
            if (starts >= 2 || alive(ours)) {
                out.state = State::Waiting;
                eprint() << "golden: its remake did not start" << (alive(ours) ? QString(" (pid %1 hangs)").arg(ours) : QString())
                         << "; its output, " << outFile() << ":\n  " << tail(outFile())
                         << "\ngolden: decks start from " << golden(out.golden) << ", not main's latest\n";
                return out;
            }
            ours = 0;
        }
        const imagebuild::Attempt last = imagebuild::attempt(kDeck);
        if (last.state == "failed" && last.source.known() && source::same(repo, last.source.commit, out.main) && !o.retry) {
            out.state = State::Failed;
            eprint() << "golden: " << golden(out.golden) << ", NOT main's latest: main " << main.describe()
                     << " did not build (image build " << kDeck << " failed at "
                     << QDateTime::fromString(last.ended, Qt::ISODate).toLocalTime().toString("yyyy-MM-dd HH:mm") << ": "
                     << last.why << "; its log: " << imagebuild::logFile(kDeck) << "). It is tried again when main moves on, "
                     << "or now: " << tool("image golden --retry") << "\n";
            return out;
        }
        const QString why = source::differs(repo, out.main);
        if (!o.start) {
            out.state = State::Stale;
            print() << "golden: " << golden(out.golden) << "; main is at " << main.describe() << ": to be made again"
                    << (why.isEmpty() ? " (" + tool("image golden") + ", or the next deck up)"
                                      : ", once the main checkout holds main's code for the deck: " + why)
                    << "\n";
            return out;
        }
        if (!why.isEmpty()) {
            out.state = State::Waiting;
            eprint() << "golden: " << golden(out.golden) << ", NOT main's latest (" << main.describe() << "): it is made "
                     << "again from the main checkout, " << repo << ", once that holds main's code for the deck, which it "
                     << "doesn't now: " << why << "\n";
            return out;
        }
        if (const QString self = foreignTool(); !self.isEmpty()) {
            out.state = State::Waiting;
            eprint() << "golden: " << golden(out.golden) << ", NOT main's latest (" << main.describe() << "): it is made "
                     << "again by main's pi-qemu, not by this build of it (" << self << "): " << kTool
                     << " image golden (on PATH), or the next " << kTool << " deck up\n";
            return out;
        }
        ours = spawn();
        starts++;
        took.start();
        print() << "golden: " << golden(out.golden) << "; main is at " << main.describe() << ": making it again from "
                << "the main checkout (image build " << kDeck << ", pid " << ours << "; a few minutes, more when its base "
                << "or most of Mixxx is built again; its log: " << imagebuild::logFile(kDeck) << ")\n";
        if (!o.wait) {
            out.state = State::Remaking;
            return out;
        }
        // Until it holds the deck's lock and has said so in its attempt, or is gone.
        for (int i = 0; i < 120 && alive(ours) && imagebuild::attempt(kDeck).pid != ours; i++) proc::sleep(250);
    }
}

} // namespace golden
