#pragma once
// pi-qemu's command line: `pi-qemu GROUP VERB ...` (deck up, release build),
// or a verb of its own (run, build-log). Each group's verbs come from its
// commands/<group>.cpp; this parses words into a call, prints the help, and
// turns a Failure into its message and exit status (2: a usage error).

#include "cli/args.h"

#include <functional>

namespace cli {

struct Verb {
    QString group;    // "deck"; empty for a verb of its own
    QString name;     // "up"
    QString synopsis; // what follows `pi-qemu GROUP VERB` in its usage line
    QString summary;  // one line, for the group's list
    QString help;     // the rest of its --help
    QVector<Option> options;
    int     optionsEnd = -1; // see Args::parse
    // Whether this call shows a window: only then is pi-qemu a GUI app, with
    // a Dock icon (main.cpp). Null: never.
    std::function<bool(const Args&)> window;
    std::function<int(Args&)> run;
};

class Registry {
public:
    void group(const QString& name, const QString& summary);
    void add(Verb v);

    struct Call {
        const Verb* verb = nullptr; // null: print `text` and exit with `status`
        Args    args;
        QString text;
        int     status = 0;
    };
    Call parse(const QStringList& words) const;
    int  run(Call& call) const;

    QString help() const;
    QString groupHelp(const QString& group) const;
    QString verbHelp(const Verb& v) const;

private:
    const Verb* find(const QString& group, const QString& name) const;
    QString usageLine(const Verb& v) const;

    QVector<QPair<QString, QString>> m_groups;
    QVector<Verb> m_verbs;
};

} // namespace cli
