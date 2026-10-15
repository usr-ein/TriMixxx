#pragma once
// Running programs: ssh to a deck, docker, git, the release container. `run`
// passes their output through to ours (the terminal, and the build log when
// one is open: util/buildlog); `capture` keeps their output; `follow` hands it
// over a line at a time, while it runs.

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

namespace proc {

enum class Input { Null, Inherit, Data, File };

struct Options {
    QString                 cwd;
    QHash<QString, QString> env;       // set in the child's environment, over ours
    QStringList             pathFirst; // directories put first in the child's PATH
    Input                   input = Input::Null;
    QByteArray              data;      // Input::Data
    QString                 file;      // Input::File
    int                     timeoutMs = -1;
    bool                    quiet = false; // capture: keep stderr too, rather than show it
};

struct Result {
    int        status = -1; // the exit status; -1: did not start, crashed or timed out
    QByteArray out, err;
    bool    ok() const { return status == 0; }
    QString text() const { return QString::fromUtf8(out).trimmed(); }
};

int    run(const QString& program, const QStringList& args, const Options& o = {});
Result capture(const QString& program, const QStringList& args, const Options& o = {});

// For a step that shows how far it has got (util/meter): like run, but what
// the program prints comes to `line` a line at a time (stderr's with `err`),
// and `tick` is called about four times a second while it runs, with how much
// of o.file (Input::File) it has taken so far. That file is fed into its stdin
// by us, to count it, as `pv FILE | program` would.
int follow(const QString& program, const QStringList& args, const Options& o,
           const std::function<void(const QString& line, bool err)>& line,
           const std::function<void(qint64 taken)>& tick);

// A shell word, quoted for sh -c (and for ssh, whose command is one).
QString quote(const QString& word);
QString join(const QStringList& words);

// The name of process `pid`'s executable ("qemu-system-aarch64"); empty if
// there is no such process.
QString name(qint64 pid);

// ^C: the children get it from the terminal and stop; pi-qemu notices and
// winds up (sleep() fails with status 130) rather than dying in the middle.
void handleInterrupt();
bool interrupted();
void sleep(int ms); // fails if interrupted

} // namespace proc
