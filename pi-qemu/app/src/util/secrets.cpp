#include "util/secrets.h"

#include "util/paths.h"

#include <QFile>
#include <QRegularExpression>

namespace secrets {

QHash<QString, QString> parse(const QString& text) {
    static const QRegularExpression assignment(R"(^\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)=(.*)$)");
    QHash<QString, QString> out;
    for (const QString& line : text.split('\n')) {
        const auto m = assignment.match(line);
        if (!m.hasMatch()) continue;
        const QString raw = m.captured(2);
        QString value;
        // sh's quoting: '...' literal, "..." with \ escapes, bare words up to a space or #.
        for (int i = 0; i < raw.size(); i++) {
            const QChar c = raw[i];
            if (c == '\'') {
                const int end = raw.indexOf('\'', i + 1);
                value += raw.mid(i + 1, (end < 0 ? raw.size() : end) - i - 1);
                i = end < 0 ? raw.size() : end;
            } else if (c == '"') {
                for (i++; i < raw.size() && raw[i] != '"'; i++) {
                    if (raw[i] == '\\' && i + 1 < raw.size() && QString("\"\\$`").contains(raw[i + 1])) i++;
                    value += raw[i];
                }
            } else if (c.isSpace() || c == '#') {
                break;
            } else {
                if (c == '\\' && i + 1 < raw.size()) i++;
                value += raw[i];
            }
        }
        out[m.captured(1)] = value;
    }
    return out;
}

QString value(const QString& key) {
    const QString env = qEnvironmentVariable(key.toUtf8().constData());
    if (!env.isEmpty()) return env;
    QFile f(paths::secretsFile());
    if (!f.open(QIODevice::ReadOnly)) return {};
    return parse(QString::fromUtf8(f.readAll())).value(key);
}

} // namespace secrets
