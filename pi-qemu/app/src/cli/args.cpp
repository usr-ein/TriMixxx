#include "cli/args.h"

#include "util/fail.h"

namespace cli {

void usage(const QString& message) { fail(message, 2); }

Args Args::parse(const QStringList& words, const QVector<Option>& options, int optionsEnd) {
    Args a;
    auto find = [&](const QString& name) -> const Option* {
        for (const Option& o : options)
            if (o.name == name) return &o;
        return nullptr;
    };
    for (int i = 0; i < words.size(); i++) {
        const QString& w = words[i];
        const bool optionsOver = optionsEnd >= 0 && a.m_pos.size() >= optionsEnd;
        if (optionsOver) { a.m_pos << w; continue; }
        if (w == "--") { a.m_rest = words.mid(i + 1); break; }
        if (w == "-h" || w == "--help") { a.m_values["help"] << QString(); continue; }
        if (!w.startsWith("--") || w.size() == 2) {
            if (w.startsWith('-') && w.size() > 1 && !w.at(1).isDigit()) usage("unknown option " + w);
            a.m_pos << w; // a word, or a negative number (jog -100)
            continue;
        }
        QString name = w.mid(2), value;
        const int eq = name.indexOf('=');
        const bool inlineValue = eq >= 0;
        if (inlineValue) { value = name.mid(eq + 1); name = name.left(eq); }
        const Option* o = find(name);
        if (!o) usage("unknown option --" + name);
        if (o->value.isEmpty()) {
            if (inlineValue) usage("--" + name + " takes no value");
            a.m_values[name] << QString();
            continue;
        }
        if (!inlineValue) {
            if (i + 1 >= words.size()) usage("--" + name + " needs " + o->value);
            value = words[++i];
        }
        a.m_values[name] << value;
    }
    return a;
}

QString Args::value(const QString& name, const QString& fallback) const {
    const QStringList v = m_values.value(name);
    return v.isEmpty() ? fallback : v.last();
}

int Args::number(const QString& name, int fallback) const {
    if (!has(name)) return fallback;
    bool ok = false;
    const int n = value(name).toInt(&ok);
    if (!ok) usage("--" + name + " takes a number, not " + value(name));
    return n;
}

QString Args::take(const QString& what) {
    if (m_pos.isEmpty()) usage("missing " + what);
    return m_pos.takeFirst();
}

QString Args::takeOr(const QString& fallback) { return m_pos.isEmpty() ? fallback : m_pos.takeFirst(); }

int Args::takeNumber(const QString& what, std::optional<int> fallback) {
    if (m_pos.isEmpty() && fallback) return *fallback;
    const QString w = take(what);
    bool ok = false;
    const int n = w.toInt(&ok);
    if (!ok) usage(what + " is a number, not " + w);
    return n;
}

QStringList Args::takeAll() {
    QStringList all = m_pos;
    m_pos.clear();
    return all;
}

void Args::done() const {
    if (!m_pos.isEmpty()) usage("unexpected " + m_pos.join(' '));
}

} // namespace cli
