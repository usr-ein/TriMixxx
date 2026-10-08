#include "util/sshtarget.h"

#include "util/fail.h"
#include "util/tool.h"

#include <QDir>
#include <QFile>

QStringList SshTarget::args(const QStringList& options, const QString& command) const {
    QStringList a;
    if (!config.isEmpty()) a << "-F" << config;
    a << options << alias;
    if (!command.isEmpty()) a << command;
    return a;
}

int SshTarget::run(const QString& command, const proc::Options& o) const {
    return proc::run("ssh", args({}, command), o);
}

proc::Result SshTarget::capture(const QString& command, const proc::Options& o) const {
    return proc::capture("ssh", args({}, command), o);
}

bool SshTarget::reachable(int seconds) const {
    proc::Options o;
    o.quiet = true;
    o.timeoutMs = (seconds + 5) * 1000;
    return proc::capture("ssh", args({"-o", QString("ConnectTimeout=%1").arg(seconds), "-o", "BatchMode=yes"}, "true"), o).ok();
}

int SshTarget::scpTo(const QStringList& local, const QString& remote) const {
    QStringList a{"-q"};
    if (!config.isEmpty()) a << "-F" << config;
    return proc::run("scp", a + local + QStringList{alias + ":" + remote});
}

int SshTarget::scpFrom(const QString& remote, const QString& local) const {
    QStringList a{"-q"};
    if (!config.isEmpty()) a << "-F" << config;
    return proc::run("scp", a + QStringList{alias + ":" + remote, local});
}

void SshTarget::writeWrappers(const QString& dir) const {
    QDir().mkpath(dir);
    for (const QString tool : {"ssh", "scp"}) {
        QString body;
        if (!config.isEmpty()) {
            // The instance's ssh_config knows both names.
            body = QString("#!/bin/sh\nexec /usr/bin/%1 -F %2 \"$@\"\n").arg(tool, proc::quote(config));
        } else {
            // `deck` and `deck:PATH` become the alias; every other word stays.
            body = QString("#!/bin/sh\n"
                           "# %1 to %2, also as `deck` (" + QString(kTool) + "'s deploy steps)\n"
                           "for a; do\n"
                           "    case \"$a\" in deck) a=%2 ;; deck:*) a=%2:\"${a#deck:}\" ;; esac\n"
                           "    set -- \"$@\" \"$a\"; shift\n"
                           "done\n"
                           "exec /usr/bin/%1 \"$@\"\n").arg(tool, proc::quote(alias));
        }
        QFile f(QDir(dir).filePath(tool));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) fail("cannot write " + f.fileName());
        f.write(body.toUtf8());
        f.close();
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup |
                         QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
    }
}

void requireUsableKey(const QString& key) {
    if (!QFile::exists(key) || !QFile::exists(key + ".pub"))
        fail("no ssh key at " + key + " (and .pub): set SSH_KEY to the key the cards trust");
    proc::Options o;
    o.quiet = true;
    if (proc::capture("ssh-keygen", {"-y", "-P", "", "-f", key}, o).ok()) return; // no passphrase
    const QString fingerprint = proc::capture("ssh-keygen", {"-lf", key + ".pub"}, o).text().section(' ', 1, 1);
    const QString agent = proc::capture("ssh-add", {"-l"}, o).text();
    if (fingerprint.isEmpty() || !agent.contains(fingerprint))
        fail(key + " has a passphrase and is not in the ssh agent: ssh-add " + key);
}
