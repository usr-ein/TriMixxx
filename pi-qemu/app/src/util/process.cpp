#include "util/process.h"

#include "util/fail.h"
#include "util/tool.h"

#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>

#include <atomic>
#include <csignal>

#ifdef __APPLE__
#include <libproc.h>
#endif

namespace proc {

namespace {

std::atomic<bool> g_interrupted{false};

void onInterrupt(int) { g_interrupted = true; }

void setUp(QProcess& p, const QString& program, const QStringList& args, const Options& o) {
    p.setProgram(program);
    p.setArguments(args);
    if (!o.cwd.isEmpty()) p.setWorkingDirectory(o.cwd);
    if (!o.env.isEmpty() || !o.pathFirst.isEmpty()) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (auto it = o.env.begin(); it != o.env.end(); ++it) env.insert(it.key(), it.value());
        if (!o.pathFirst.isEmpty())
            env.insert("PATH", o.pathFirst.join(':') + ":" + env.value("PATH"));
        p.setProcessEnvironment(env);
    }
    switch (o.input) {
    case Input::Null: p.setStandardInputFile(QProcess::nullDevice()); break;
    case Input::Inherit: p.setInputChannelMode(QProcess::ForwardedInputChannel); break;
    case Input::File: p.setStandardInputFile(o.file); break;
    case Input::Data: break;
    }
}

int finish(QProcess& p, const Options& o, const QString& program) {
    if (!p.waitForStarted(-1)) {
        fprintf(stderr, "%s: cannot run %s: %s\n", kTool, qPrintable(program), qPrintable(p.errorString()));
        return -1;
    }
    if (o.input == Input::Data) {
        p.write(o.data);
        p.closeWriteChannel();
    }
    if (!p.waitForFinished(o.timeoutMs)) {
        p.kill();
        p.waitForFinished(2000);
        return -1;
    }
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}

} // namespace

int run(const QString& program, const QStringList& args, const Options& o) {
    QProcess p;
    setUp(p, program, args, o);
    p.setProcessChannelMode(QProcess::ForwardedChannels);
    p.start();
    return finish(p, o, program);
}

Result capture(const QString& program, const QStringList& args, const Options& o) {
    QProcess p;
    setUp(p, program, args, o);
    p.setProcessChannelMode(o.quiet ? QProcess::SeparateChannels : QProcess::ForwardedErrorChannel);
    p.start();
    Result r;
    r.status = finish(p, o, program);
    r.out = p.readAllStandardOutput();
    if (o.quiet) r.err = p.readAllStandardError();
    return r;
}

QString quote(const QString& word) {
    static const QString safe = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@%_-+=:,./";
    if (!word.isEmpty() && std::all_of(word.begin(), word.end(), [](QChar c) { return safe.contains(c); }))
        return word;
    QString q = word;
    q.replace("'", "'\\''");
    return "'" + q + "'";
}

QString join(const QStringList& words) {
    QStringList q;
    for (const QString& w : words) q << quote(w);
    return q.join(' ');
}

QString name(qint64 pid) {
    if (pid <= 0) return {};
#ifdef __APPLE__
    char path[PROC_PIDPATHINFO_MAXSIZE];
    const int n = proc_pidpath(int(pid), path, sizeof path);
    return n > 0 ? QString::fromUtf8(path, n).section('/', -1) : QString();
#else
    return QFileInfo(QString("/proc/%1/exe").arg(pid)).symLinkTarget().section('/', -1);
#endif
}

void handleInterrupt() {
    std::signal(SIGINT, onInterrupt);
    std::signal(SIGTERM, onInterrupt);
}

bool interrupted() { return g_interrupted; }

void sleep(int ms) {
    for (int left = ms; left > 0; left -= 50) {
        if (g_interrupted) fail("interrupted", 130);
        QThread::msleep(qMin(left, 50));
    }
    if (g_interrupted) fail("interrupted", 130);
}

} // namespace proc
