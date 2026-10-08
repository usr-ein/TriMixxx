#pragma once
// How the Mac reaches a deck over ssh. An emulated deck: its instance's own
// ssh_config (Host NAME deck), ~/.ssh/config untouched. A real deck: its
// alias in ~/.ssh/config. Either way the wrappers in binDir answer to
// `deck` -- what deploy steps call -- and to the deck's own name.

#include "util/process.h"

#include <QString>
#include <QStringList>

struct SshTarget {
    QString alias;  // what ssh is given: an instance's NAME, or a real deck's alias
    QString config; // ssh -F; empty: ~/.ssh/config

    // ssh [-F config] [options] alias [command]
    QStringList args(const QStringList& options = {}, const QString& command = {}) const;
    // A command on the deck, its output passed through; its exit status.
    int run(const QString& command, const proc::Options& o = {}) const;
    proc::Result capture(const QString& command, const proc::Options& o = {}) const;
    // ssh answers within `seconds`.
    bool reachable(int seconds = 2) const;
    int scpTo(const QStringList& local, const QString& remote) const; // remote: a path on the deck
    int scpFrom(const QString& remote, const QString& local) const;

    // bin/ssh and bin/scp under `dir`: plain ssh and scp, reaching this deck as
    // `deck` (and as alias).
    void writeWrappers(const QString& dir) const;
};

// Fails at once, saying what to do, if ssh could not log in with `key`
// without asking: one with a passphrase must be in the agent (ssh-add).
void requireUsableKey(const QString& key);
