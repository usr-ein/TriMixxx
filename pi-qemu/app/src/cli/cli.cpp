#include "cli/cli.h"

#include "util/fail.h"
#include "util/tool.h"

#include <QTextStream>

#include <cstdio>

namespace cli {

namespace {

bool isHelp(const QString& w) { return w == "help" || w == "-h" || w == "--help"; }

QString wrap(const QString& text, int indent) {
    // Help text is written with its own line breaks; only indent it.
    QStringList lines = text.split('\n');
    for (QString& l : lines)
        if (!l.isEmpty()) l.prepend(QString(indent, ' '));
    return lines.join('\n');
}

} // namespace

void Registry::group(const QString& name, const QString& summary) { m_groups.append({name, summary}); }

void Registry::add(Verb v) { m_verbs.append(std::move(v)); }

const Verb* Registry::find(const QString& group, const QString& name) const {
    for (const Verb& v : m_verbs)
        if (v.group == group && v.name == name) return &v;
    return nullptr;
}

QString Registry::usageLine(const Verb& v) const {
    QString line = QString(kTool) + " " + (v.group.isEmpty() ? QString() : v.group + " ") + v.name;
    if (!v.synopsis.isEmpty()) line += " " + v.synopsis;
    return line;
}

QString Registry::help() const {
    QString t = QString(kTool) + ": TriMixxx's tool -- decks emulated and real, deploying, images, releases.\n\n";
    for (const auto& [name, summary] : m_groups)
        t += ("  " + tool(name + " VERB ...")).leftJustified(34) + summary + "\n";
    for (const Verb& v : m_verbs)
        if (v.group.isEmpty()) t += ("  " + usageLine(v)).leftJustified(34) + v.summary + "\n";
    t += QString("\n`%1 GROUP` lists a group's verbs; `%1 GROUP VERB --help` says more about one.\n"
                 "A TARGET is an emulated deck's NAME (%1 deck list), or --host ALIAS for a real\n"
                 "deck by its ssh alias. There is no default deck.\n").arg(kTool);
    return t;
}

QString Registry::groupHelp(const QString& group) const {
    QString summary;
    for (const auto& [name, s] : m_groups)
        if (name == group) summary = s;
    QString t = tool(group) + ": " + summary + "\n\n";
    for (const Verb& v : m_verbs) {
        if (v.group != group) continue;
        t += "  " + v.name + (v.synopsis.isEmpty() ? QString() : " " + v.synopsis) + "\n";
        t += "      " + v.summary + "\n";
    }
    t += "\n" + tool(group) + " VERB --help: more about one.\n";
    return t;
}

QString Registry::verbHelp(const Verb& v) const {
    QString t = "usage: " + usageLine(v) + "\n\n" + v.summary + "\n";
    if (!v.help.isEmpty()) t += "\n" + v.help.trimmed() + "\n";
    if (!v.options.isEmpty()) {
        t += "\noptions:\n";
        for (const Option& o : v.options) {
            const QString head = "  --" + o.name + (o.value.isEmpty() ? QString() : " " + o.value);
            if (head.size() < 24) {
                t += head.leftJustified(24) + o.help.section('\n', 0, 0) + "\n";
                if (o.help.contains('\n')) t += wrap(o.help.section('\n', 1), 24) + "\n";
            } else {
                t += head + "\n" + wrap(o.help, 24) + "\n";
            }
        }
    }
    return t;
}

Registry::Call Registry::parse(const QStringList& words) const {
    Call c;
    if (words.isEmpty()) { c.text = help(); c.status = 2; return c; }
    QString first = words[0];
    if (isHelp(first)) {
        bool group = words.size() > 1 && std::any_of(m_groups.begin(), m_groups.end(),
                                                     [&](const auto& g) { return g.first == words[1]; });
        c.text = group ? groupHelp(words[1]) : help();
        return c;
    }
    const Verb* v = nullptr;
    QStringList rest;
    if (std::any_of(m_groups.begin(), m_groups.end(), [&](const auto& g) { return g.first == first; })) {
        if (words.size() < 2 || isHelp(words[1])) {
            c.text = groupHelp(first);
            c.status = words.size() < 2 ? 2 : 0;
            return c;
        }
        v = find(first, words[1]);
        if (!v) {
            c.text = QString("%1: no verb %2\n\n").arg(tool(first), words[1]) + groupHelp(first);
            c.status = 2;
            return c;
        }
        rest = words.mid(2);
    } else {
        v = find({}, first);
        if (!v) {
            c.text = QString(kTool) + ": no command " + first + "\n\n" + help();
            c.status = 2;
            return c;
        }
        rest = words.mid(1);
    }
    try {
        c.args = Args::parse(rest, v->options, v->optionsEnd);
    } catch (const Failure& f) {
        c.text = QString("%1: %2\nusage: %3   (--help for more)\n")
                     .arg(tool(v->group.isEmpty() ? v->name : v->group + " " + v->name), f.message, usageLine(*v));
        c.status = 2;
        return c;
    }
    if (c.args.has("help")) { c.text = verbHelp(*v); return c; }
    c.verb = v;
    return c;
}

int Registry::run(Call& c) const {
    if (!c.verb) {
        QTextStream(c.status == 0 ? stdout : stderr) << c.text;
        return c.status;
    }
    try {
        return c.verb->run(c.args);
    } catch (const Failure& f) {
        fflush(stdout);
        if (f.status == 2)
            QTextStream(stderr) << tool(c.verb->group.isEmpty() ? c.verb->name : c.verb->group + " " + c.verb->name)
                                << ": " << f.message << "\nusage: " << usageLine(*c.verb) << "   (--help for more)\n";
        else
            QTextStream(stderr) << kTool << ": " << f.message << "\n";
        return f.status;
    }
}

} // namespace cli
