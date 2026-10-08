#include "util/buildlog.h"

#include "util/fail.h"
#include "util/paths.h"
#include "util/process.h"
#include "util/tool.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTime>

#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace buildlog {

void say(const QString& text) {
    printf("\n==> [%s] %s\n", qPrintable(QTime::currentTime().toString("HH:mm:ss")), qPrintable(text));
    fflush(stdout);
}

namespace {

// Our stdout and stderr into `tee -a log`, which also writes them where they
// went before. Returns tee's pid; `saved` gets the old stdout and stderr.
pid_t teeInto(const QString& log, int saved[2]) {
    int fds[2];
    if (::pipe(fds) != 0) fail("cannot make a pipe for the build log");
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[0], 0);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    posix_spawn_file_actions_addclose(&fa, fds[1]);
    const QByteArray path = QFile::encodeName(log);
    char* argv[] = {const_cast<char*>("tee"), const_cast<char*>("-a"), const_cast<char*>(path.constData()), nullptr};
    // A ^C stops the build, not its log: tee ignores it, and sees the end of
    // its input when the build is over.
    void (*old)(int) = std::signal(SIGINT, SIG_IGN);
    pid_t pid = 0;
    const int err = posix_spawn(&pid, "/usr/bin/tee", &fa, nullptr, argv, environ);
    std::signal(SIGINT, old);
    posix_spawn_file_actions_destroy(&fa);
    ::close(fds[0]);
    if (err != 0) { ::close(fds[1]); fail("cannot start tee for the build log"); }
    fflush(stdout);
    fflush(stderr);
    saved[0] = ::dup(1);
    saved[1] = ::dup(2);
    ::dup2(fds[1], 1);
    ::dup2(fds[1], 2);
    ::close(fds[1]);
    return pid;
}

void untee(pid_t tee, const int saved[2]) {
    fflush(stdout);
    fflush(stderr);
    ::dup2(saved[0], 1);
    ::dup2(saved[1], 2);
    ::close(saved[0]);
    ::close(saved[1]);
    // tee ends with its input. A program still holding the pipe (it should
    // not) would keep it open: then it is stopped, after a while.
    for (int i = 0; i < 100; i++) {
        if (::waitpid(tee, nullptr, WNOHANG) == tee) return;
        ::usleep(50000);
    }
    ::kill(tee, SIGTERM);
    ::waitpid(tee, nullptr, 0);
}

void openWindow(const QString& log) {
    // One window per log: a second build on it reuses the one open.
    proc::Options o;
    o.quiet = true;
    if (proc::capture("pgrep", {"-f", "build-log " + log}, o).ok()) return;
    QProcess p;
    p.setProgram(paths::binary());
    p.setArguments({"build-log", log});
    p.setStandardInputFile(QProcess::nullDevice());
    p.setStandardOutputFile(QProcess::nullDevice());
    p.setStandardErrorFile(QProcess::nullDevice());
    // Its own session: a ^C to the build leaves it showing how the build ended.
    p.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
    p.startDetached();
}

} // namespace

int run(const QString& logPath, bool window, const QStringList& plan, const std::function<void()>& build) {
    QDir().mkpath(QFileInfo(logPath).absolutePath());
    QFile f(logPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) fail("cannot write " + logPath);
    f.close();
    int saved[2];
    const pid_t tee = teeInto(logPath, saved);
    if (window && qEnvironmentVariable("BUILD_WINDOW") != "0") openWindow(QFileInfo(logPath).absoluteFilePath());
    int status = 0;
    try {
        if (!plan.isEmpty()) printf("==> plan: %s\n", qPrintable(plan.join(" | ")));
        fflush(stdout);
        build();
    } catch (const Failure& e) {
        fflush(stdout);
        fprintf(stderr, "%s: %s\n", kTool, qPrintable(e.message));
        status = e.status;
    }
    printf("BUILD_EXIT %d\n", status);
    untee(tee, saved);
    return status;
}

} // namespace buildlog
