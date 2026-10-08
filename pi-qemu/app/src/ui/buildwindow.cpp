#include "ui/buildwindow.h"

#include "util/tool.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSyntaxHighlighter>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// The build's steps, in order, and the text of the "==>" line that starts each
// (image/build.sh's say, pi-qemu/deploy.sh's say, release/Makefile's say). A
// build runs some of them: a cached base skips the seeding and the first boot,
// only trimixxx0's ends with the golden snapshot, only `make release` seals a
// release. A step never started shows as skipped.
struct Step { const char* name; const char* marker; };
constexpr Step kSteps[] = {
    {"Stock Raspberry Pi OS card", "base image"},
    {"Seed: user, hostname, network", "seeding cloud-init"},
    {"First boot", "booting the stock card"},
    {"Base: fresh-install.md 1-2", "base (fresh-install.md"},
    {"Reboot into the deck's boot flags", "into the deck's boot flags"},
    {"ttymidi", ": ttymidi"},
    {"System: pi_config/upload.sh", "system (pi_config/upload.sh)"},
    {"Launch manager", ": launcher"},
    {"Mixxx fork (Docker build)", "Mixxx fork"},
    {"Mixxx config, skin, wiring", "Mixxx config"},
    {"Music library directory", "music library directory"},
    {"Doom", ": Doom"},
    {"Seal and power off", "sealing"},
    {"Golden snapshot for agents", "golden snapshot"},
    {"Release: the system, as SquashFS", "release: the system"},
    {"Release: the card (genimage)", "release: the card"},
    {"Release: the update (RAUC bundle)", "release: the update"},
};
constexpr int kStepCount = int(sizeof(kSteps) / sizeof(kSteps[0]));

// ---- bash, as a terminal would colour it -------------------------------------
class BashHighlighter : public QSyntaxHighlighter {
public:
    explicit BashHighlighter(QTextDocument* d) : QSyntaxHighlighter(d) {
        auto fmt = [](QColor c, bool bold = false, bool italic = false) {
            QTextCharFormat f;
            f.setForeground(c);
            if (bold) f.setFontWeight(QFont::Bold);
            f.setFontItalic(italic);
            return f;
        };
        m_keyword = fmt(QColor(198, 120, 221), true);
        m_string = fmt(QColor(152, 195, 121));
        m_variable = fmt(QColor(86, 182, 194));
        m_comment = fmt(QColor(110, 115, 125), false, true);
        m_trace = fmt(QColor(150, 150, 155));
        m_stage = fmt(QColor(97, 175, 239), true);
        m_error = fmt(QColor(240, 85, 85), true);
        m_ok = fmt(QColor(120, 220, 120), true);
        m_docker = fmt(QColor(130, 130, 140));
    }

protected:
    void highlightBlock(const QString& t) override {
        static const QRegularExpression kw(
            R"(\b(if|then|else|elif|fi|for|while|until|do|done|case|esac|in|function|return|exit|set|export|local|sudo|ssh|scp|make|install|echo|printf|cd|test)\b)");
        static const QRegularExpression str(R"("(?:[^"\\]|\\.)*"|'[^']*')");
        static const QRegularExpression var(R"(\$\{[^}]*\}|\$[A-Za-z_][A-Za-z_0-9]*|\$\(|\$[0-9?#@*])");
        static const QRegularExpression err(R"(\b(ERROR|Error|error:|FAILED|failed|fatal|Fatal|cannot|No such file)\b)");

        if (t.startsWith("==>")) { setFormat(0, t.size(), m_stage); return; }
        if (t.startsWith("BUILD_EXIT")) { setFormat(0, t.size(), t.endsWith(" 0") ? m_ok : m_error); return; }
        if (err.match(t).hasMatch()) { setFormat(0, t.size(), m_error); return; }
        if (QRegularExpression(R"(^#\d+ )").match(t).hasMatch()) { setFormat(0, t.size(), m_docker); return; }
        bool trace = t.startsWith('+');
        if (trace) setFormat(0, t.size(), m_trace);
        for (auto it = kw.globalMatch(t); it.hasNext();) { auto m = it.next(); setFormat(m.capturedStart(), m.capturedLength(), m_keyword); }
        for (auto it = var.globalMatch(t); it.hasNext();) { auto m = it.next(); setFormat(m.capturedStart(), m.capturedLength(), m_variable); }
        for (auto it = str.globalMatch(t); it.hasNext();) { auto m = it.next(); setFormat(m.capturedStart(), m.capturedLength(), m_string); }
        // A comment: # at the start of the line or after whitespace, outside a string.
        static const QRegularExpression comment(R"((^|\s)#(?!\d+ ).*$)");
        if (auto m = comment.match(t); m.hasMatch())
            setFormat(m.capturedStart(), m.capturedLength(), m_comment);
    }

private:
    QTextCharFormat m_keyword, m_string, m_variable, m_comment, m_trace, m_stage, m_error, m_ok, m_docker;
};

QString hms(qint64 s) {
    return QString("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
}

} // namespace

BuildWindow::BuildWindow(const QString& logPath, QWidget* parent)
    : QWidget(parent), m_path(logPath), m_seen(kStepCount, false) {
    setWindowTitle(QString(kTool) + " — the build");
    resize(1250, 780);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_log->setMaximumBlockCount(200000);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(12);
    m_log->setFont(mono);
    m_log->setStyleSheet("QPlainTextEdit { background: #000; color: #e8e8e8; border: none; }");
    new BashHighlighter(m_log->document());

    m_bar = new QProgressBar(this);
    m_bar->setRange(0, kStepCount);
    m_bar->setValue(0);
    m_bar->setTextVisible(true);
    m_bar->setFormat(QString("0 / %1").arg(kStepCount));
    m_step = new QLabel("waiting for the build to start…", this);
    m_elapsed = new QLabel("elapsed 0:00:00", this);
    QFont big = m_elapsed->font();
    big.setPointSize(big.pointSize() + 2);
    big.setBold(true);
    m_elapsed->setFont(big);
    m_step->setFont(big);

    m_steps = new QListWidget(this);
    m_steps->setFixedWidth(300);
    m_steps->setFocusPolicy(Qt::NoFocus);
    for (const auto& s : kSteps) m_steps->addItem(QString("·  ") + s.name);

    auto* top = new QHBoxLayout;
    top->addWidget(m_step, 1);
    top->addWidget(m_elapsed);
    auto* right = new QVBoxLayout;
    right->addLayout(top);
    right->addWidget(m_bar);
    right->addWidget(m_log, 1);
    auto* root = new QHBoxLayout(this);
    root->addWidget(m_steps);
    root->addLayout(right, 1);

    m_clock = new QTimer(this);
    connect(m_clock, &QTimer::timeout, this, &BuildWindow::tick);
    m_clock->start(1000);
    auto* reader = new QTimer(this);
    connect(reader, &QTimer::timeout, this, &BuildWindow::poll);
    reader->start(300);
    poll();
}

void BuildWindow::poll() {
    if (!m_file.isOpen()) {
        m_file.setFileName(m_path);
        if (!m_file.open(QIODevice::ReadOnly)) return;
    }
    if (m_file.size() < m_pos) { // a new build started over the same log
        m_pos = 0;
        m_partial.clear();
        m_log->clear();
        m_current = -1;
        m_seen.fill(false);
        m_finished = false;
        m_start = m_last = {};
        for (int i = 0; i < m_steps->count(); i++) m_steps->item(i)->setText(QString("·  ") + kSteps[i].name);
    }
    m_file.seek(m_pos);
    QByteArray chunk = m_file.readAll();
    if (chunk.isEmpty()) return;
    m_pos += chunk.size();
    m_partial += chunk;

    QScrollBar* sb = m_log->verticalScrollBar();
    const bool atBottom = sb->value() >= sb->maximum() - 2; // follow unless the user scrolled up
    int nl;
    QStringList lines;
    while ((nl = m_partial.indexOf('\n')) >= 0) {
        QString line = QString::fromUtf8(m_partial.left(nl));
        m_partial.remove(0, nl + 1);
        line.remove('\r');
        lines << line;
        onLine(line);
    }
    if (!lines.isEmpty()) m_log->appendPlainText(lines.join('\n'));
    if (atBottom) sb->setValue(sb->maximum());
}

void BuildWindow::onLine(const QString& line) {
    static const QRegularExpression stamp(R"(^==> \[(\d\d):(\d\d):(\d\d)\])");
    if (auto m = stamp.match(line); m.hasMatch()) {
        // The build's own clock: its stage lines' stamps.
        QDateTime at(QDate::currentDate(), QTime(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()));
        if (at > QDateTime::currentDateTime().addSecs(60)) at = at.addDays(-1);
        if (!m_start.isValid()) m_start = at;
        m_last = at;
    }
    if (line.startsWith("==>")) {
        for (int i = qMax(m_current, 0); i < kStepCount; i++)
            if (line.contains(kSteps[i].marker)) { setStep(i); break; }
    }
    if (line.startsWith("BUILD_EXIT")) {
        m_finished = true;
        const bool ok = line.endsWith(" 0");
        if (ok) {
            for (int i = 0; i < kStepCount; i++) m_steps->item(i)->setText(QString(m_seen[i] ? "✓  " : "–  ") + kSteps[i].name);
            m_bar->setValue(kStepCount);
            m_bar->setFormat("done");
        } else if (m_current >= 0) {
            m_steps->item(m_current)->setText(QString("✗  ") + kSteps[m_current].name);
        }
        m_step->setText(ok ? "Built." : "The build failed — see the end of the log.");
        m_step->setStyleSheet(ok ? "color: rgb(60,200,60)" : "color: rgb(230,70,70)");
        m_bar->setStyleSheet(ok ? "QProgressBar::chunk { background: rgb(60,170,60); }"
                                : "QProgressBar::chunk { background: rgb(200,60,60); }");
        tick();
    }
}

void BuildWindow::setStep(int index) {
    m_seen[index] = true;
    for (int i = 0; i < kStepCount; i++)
        m_steps->item(i)->setText(QString(i < index ? (m_seen[i] ? "✓  " : "–  ") : i == index ? "▶  " : "·  ") + kSteps[i].name);
    m_current = index;
    m_bar->setValue(index);
    m_bar->setFormat(QString("%1 / %2").arg(index).arg(kStepCount));
    m_step->setText(QString("Step %1 of %2: %3").arg(index + 1).arg(kStepCount).arg(kSteps[index].name));
    m_steps->setCurrentRow(index);
}

void BuildWindow::tick() {
    if (!m_start.isValid()) return;
    m_elapsed->setText(m_finished ? "took " + hms(m_start.secsTo(m_last))
                                  : "elapsed " + hms(m_start.secsTo(QDateTime::currentDateTime())));
    if (m_finished) m_clock->stop();
}
