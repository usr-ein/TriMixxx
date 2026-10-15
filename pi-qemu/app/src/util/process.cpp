#include "util/process.h"

#include "util/fail.h"
#include "util/tool.h"

#include <QElapsedTimer>
#include <QFile>
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

int follow(const QString& program, const QStringList& args, const Options& o,
           const std::function<void(const QString& line, bool err)>& line,
           const std::function<void(qint64 taken)>& tick) {
    constexpr qint64 kPiece = 1 << 20; // the file read 1 MB at a time,
    constexpr qint64 kAhead = 4 << 20; // with at most 4 MB of it waiting in QProcess
    QFile in(o.file);
    if (o.input == Input::File && !in.open(QIODevice::ReadOnly)) {
        fprintf(stderr, "%s: cannot read %s: %s\n", kTool, qPrintable(o.file), qPrintable(in.errorString()));
        return -1;
    }
    Options piped = o;
    if (o.input == Input::File) piped.input = Input::Data; // its stdin a pipe, fed below
    QProcess p;
    setUp(p, program, args, piped);
    p.start();
    if (!p.waitForStarted(-1)) {
        fprintf(stderr, "%s: cannot run %s: %s\n", kTool, qPrintable(program), qPrintable(p.errorString()));
        return -1;
    }
    if (o.input == Input::Data) {
        p.write(o.data);
        p.closeWriteChannel();
    }
    // Its output's complete lines, handed over; at its end, the rest too.
    QByteArray partial[2];
    const auto relay = [&](bool err, bool end) {
        QByteArray& b = partial[err];
        b += err ? p.readAllStandardError() : p.readAllStandardOutput();
        for (qsizetype nl; (nl = b.indexOf('\n')) >= 0; b.remove(0, nl + 1))
            line(QString::fromUtf8(b.left(nl)).remove(u'\r'), err);
        if (end && !b.isEmpty()) {
            line(QString::fromUtf8(b).remove(u'\r'), err);
            b.clear();
        }
    };
    qint64 fed = 0; // of the file, into QProcess: it has taken all but what still waits there
    bool failed = false;
    QElapsedTimer clock, sinceTick;
    clock.start();
    sinceTick.start();
    while (p.state() != QProcess::NotRunning) {
        while (in.isOpen() && p.bytesToWrite() < kAhead) {
            const QByteArray piece = in.read(kPiece);
            if (piece.isEmpty()) {
                if (!in.atEnd()) { // the Mac's disk failed us: stopped, rather than waited on forever
                    fprintf(stderr, "%s: cannot read %s: %s\n", kTool, qPrintable(o.file), qPrintable(in.errorString()));
                    failed = true;
                    p.kill();
                }
                in.close();
                p.closeWriteChannel(); // its input ends once what waits is written
                break;
            }
            p.write(piece);
            fed += piece.size();
        }
        if (o.timeoutMs >= 0 && clock.elapsed() > o.timeoutMs && !failed) {
            failed = true;
            p.kill();
        }
        if (p.bytesToWrite() > 0) p.waitForBytesWritten(250);
        else p.waitForFinished(250);
        relay(false, false);
        relay(true, false);
        if (tick && sinceTick.elapsed() >= 250) {
            tick(fed - p.bytesToWrite());
            sinceTick.restart();
        }
    }
    relay(false, true);
    relay(true, true);
    if (tick) tick(fed - p.bytesToWrite());
    if (failed) return -1;
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
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
