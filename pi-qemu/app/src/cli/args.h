#pragma once
// A verb's words, parsed against the options it takes: --flag, --name VALUE
// or --name=VALUE, the positionals in order, and the words after `--` apart.
// -h/--help is every verb's.

#include <QHash>
#include <QStringList>
#include <QVector>

#include <optional>

namespace cli {

struct Option {
    QString name;   // without the dashes: "window"
    QString value;  // its value's placeholder ("CARD"); empty for a flag
    QString help;
};

class Args {
public:
    // optionsEnd: once this many positionals are in, every later word is one
    // too, dashes and all (a command for the deck); -1: options anywhere.
    // Fails (status 2) on an unknown option or a missing value.
    static Args parse(const QStringList& words, const QVector<Option>& options, int optionsEnd = -1);

    bool    has(const QString& name) const { return m_values.contains(name); }
    QString value(const QString& name, const QString& fallback = {}) const;
    QStringList values(const QString& name) const { return m_values.value(name); }
    int     number(const QString& name, int fallback) const; // fails on a non-number

    const QStringList& positional() const { return m_pos; }
    bool    more() const { return !m_pos.isEmpty(); }
    QString take(const QString& what);                // the next positional; fails naming `what`
    QString takeOr(const QString& fallback);          // the next positional, or `fallback`
    int     takeNumber(const QString& what, std::optional<int> fallback = {});
    QStringList takeAll();                            // every positional left
    const QStringList& afterDashes() const { return m_rest; }
    void    done() const;                             // fails if positionals are left over

private:
    QHash<QString, QStringList> m_values;
    QStringList m_pos, m_rest;
};

// A usage error: status 2.
[[noreturn]] void usage(const QString& message);

} // namespace cli
