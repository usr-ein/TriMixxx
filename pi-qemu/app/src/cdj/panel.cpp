#include "cdj/panel.h"

#include <QSet>

namespace cdj {

const QVector<Key>& keys() {
    // nxs_panel.KEY_NAMES: the NXS's own service-table names, where MAIN's
    // decoder 0x042f5810 finds each contact (docs/NXS_PANEL_MAP.md).
    static const QVector<Key> all{
        {"LOCK", 15, 0x01},          {"REV", 15, 0x02},           {"JOG TOUCH SW", 15, 0x20},
        {"PLAY", 16, 0x01},          {"CUE", 16, 0x02},           {"RELOOP", 16, 0x04},
        {"OUT", 16, 0x08},           {"IN", 16, 0x10},            {"HOT CUE A", 16, 0x20},
        {"HOT CUE B", 16, 0x40},     {"HOT CUE C", 16, 0x80},     {"ENCODER PUSH", 17, 0x01},
        {"LOOP MODE", 17, 0x02},     {"SD OPEN", 17, 0x04},       {"SLIP", 17, 0x08},
        {"QUANTIZE", 18, 0x01},      {"REC MODE", 18, 0x02},      {"PREVIOUS |<<", 18, 0x04},
        {"NEXT >>|", 18, 0x08},      {"REV <<", 18, 0x10},        {"FWD >>", 18, 0x20},
        {"< CALL", 18, 0x40},        {"CALL >", 18, 0x80},        {"REKORDBOX", 19, 0x01},
        {"LINK", 19, 0x02},          {"USB", 19, 0x04},           {"SD", 19, 0x08},
        {"DISC", 19, 0x10},          {"TIME/ACUE", 19, 0x20},     {"DELETE", 19, 0x40},
        {"MEMORY", 19, 0x80},        {"BROWSE", 20, 0x01},        {"TAG LIST", 20, 0x02},
        {"INFORMATION", 20, 0x04},   {"MENU", 20, 0x08},          {"RETURN", 20, 0x10},
        {"TAG TRACK", 20, 0x20},     {"EJECT", 20, 0x40},         {"JOG MODE", 21, 0x01},
        {"SYNC", 21, 0x02},          {"MASTER", 21, 0x04},        {"TEMPO RANGE", 21, 0x08},
        {"MASTER TEMPO", 21, 0x10},  {"TEMPO RESET", 21, 0x20},
    };
    return all;
}

const Key* key(const QString& name) {
    QString k = name.trimmed().toLower().replace('_', ' ').replace('-', ' ');
    static const QHash<QString, QString> aliases{{"enter", "encoder push"}, {"back", "return"}, {"info", "information"}};
    k = aliases.value(k, k);
    for (const Key& each : keys())
        if (each.name.toLower() == k) return &each;
    return nullptr;
}

static QString contact(const Key& k) { return QString("%1 %2").arg(k.byte).arg(k.mask, 2, 16, QChar('0')); }

QString downLine(const Key& k) { return "down " + contact(k); }
QString upLine(const Key& k) { return "up " + contact(k); }
QString rotaryLine(int detents) { return QString("rotary 7 %1").arg(detents); }
QString tempoCentreLine() { return QString("analog 3 %1").arg(kTempoCentre); }
QString tempoLine(int position) { return QString("analog 2 %1").arg(qBound(0, position, 65535)); }

QHash<QString, QString> stateOf(const QString& stateReply) {
    QHash<QString, QString> fields;
    const QStringList words = stateReply.split(' ', Qt::SkipEmptyParts);
    if (words.size() < 2 || words[0] != "ok" || words[1] != "state") return fields;
    for (const QString& word : words.mid(2)) {
        const qsizetype eq = word.indexOf('=');
        if (eq > 0) fields.insert(word.left(eq), word.mid(eq + 1));
    }
    return fields;
}

QHash<QString, QString> lampsOf(const QString& stateReply) {
    QHash<QString, QString> lit;
    const QString lamps = stateOf(stateReply).value("lamps");
    for (const QString& item : lamps.split(',', Qt::SkipEmptyParts)) {
        const qsizetype colon = item.indexOf(':');
        if (colon > 0) lit.insert(item.left(colon), item.mid(colon + 1));
    }
    return lit;
}

int analogTarget(const QHash<QString, QString>& state, int field) {
    const QString f = state.value(QString("a%1").arg(field)); // "-0/0" or "32768/32768"
    if (f.isEmpty() || f.startsWith('-')) return -1;
    bool ok = false;
    const int target = f.section('/', 1, 1).toInt(&ok);
    return ok ? target : -1;
}

bool leverAtRev(const QHash<QString, QString>& state) {
    // 22 bytes as hex: byte 15 is the 31st and 32nd digits.
    const int mask = state.value("level_mask").mid(30, 2).toInt(nullptr, 16);
    const int value = state.value("level_value").mid(30, 2).toInt(nullptr, 16);
    return (mask & 0x02) && !(value & 0x02);
}

Lit lit(const QHash<QString, QString>& lamps, const QString& lamp) {
    const QString how = lamps.value(lamp);
    if (how == "blink") return Lit::Blink;
    if (how == "on" || how == "2" || how == "3") return Lit::On;
    if (how == "1") return Lit::Dim;
    return Lit::Off;
}

bool twoBit(const QString& lamp) {
    static const QSet<QString> fields{"SOURCE_REKORDBOX", "SOURCE_DISC", "SOURCE_SD", "SOURCE_USB", "SOURCE_LINK",
                                      "BROWSE", "INFO", "TAG_LIST", "MENU", "LOOP_MODE", "SYNC", "MASTER"};
    return fields.contains(lamp);
}

} // namespace cdj
