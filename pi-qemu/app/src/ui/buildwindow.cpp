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

BuildWindow::BuildWindow(const QString& logPath, QWidget* parent) : QWidget(parent), m_path(logPath) {
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
    m_bar->setRange(0, 1);
    m_bar->setValue(0);
    m_bar->setTextVisible(true);
    m_bar->setFormat("");
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
        m_finished = false;
        m_start = m_last = {};
        setPlan({});
        m_planned = false;
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
    static const QRegularExpression stamp(R"(^==> \[(\d\d):(\d\d):(\d\d)\] (.*)$)");
    if (line.startsWith("==> plan: ")) {
        setPlan(line.mid(10).split(" | ", Qt::SkipEmptyParts));
        m_planned = true;
        return;
    }
    if (auto m = stamp.match(line); m.hasMatch()) {
        // The build's own clock: its stage lines' stamps.
        QDateTime at(QDate::currentDate(), QTime(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()));
        if (at > QDateTime::currentDateTime().addSecs(60)) at = at.addDays(-1);
        if (!m_start.isValid()) m_start = at;
        m_last = at;
        // The planned step this line starts; without a plan, a step of its own.
        const QString text = m.captured(4);
        int found = -1;
        for (int i = qMax(m_current, 0); i < m_plan.size() && found < 0; i++)
            if (text.startsWith(m_plan[i])) found = i;
        if (found < 0 && !m_planned) {
            m_plan << text;
            m_seen << false;
            m_steps->addItem(text);
            found = int(m_plan.size()) - 1;
        }
        if (found >= 0) setStep(found);
    }
    if (line.startsWith("BUILD_EXIT")) {
        m_finished = true;
        m_ok = line.endsWith(" 0");
        showSteps();
        if (m_ok) {
            m_bar->setRange(0, 1);
            m_bar->setValue(1);
            m_bar->setFormat("done");
        }
        m_step->setText(m_ok ? "Built." : "The build failed — see the end of the log.");
        m_step->setStyleSheet(m_ok ? "color: rgb(60,200,60)" : "color: rgb(230,70,70)");
        m_bar->setStyleSheet(m_ok ? "QProgressBar::chunk { background: rgb(60,170,60); }"
                                  : "QProgressBar::chunk { background: rgb(200,60,60); }");
        tick();
    }
}

void BuildWindow::setPlan(const QStringList& titles) {
    m_plan = titles;
    m_seen = QVector<bool>(titles.size(), false);
    m_current = -1;
    m_steps->clear();
    for (const QString& t : titles) m_steps->addItem(t);
    m_bar->setRange(0, qMax(1, int(titles.size())));
    m_bar->setValue(0);
    m_bar->setFormat(titles.isEmpty() ? QString() : QString("0 / %1").arg(titles.size()));
    m_bar->setStyleSheet({});
    m_step->setText("waiting for the build to start…");
    m_step->setStyleSheet({});
    showSteps();
}

// Each step's mark: done, skipped, running, failed, to come.
void BuildWindow::showSteps() {
    for (int i = 0; i < m_plan.size(); i++) {
        QString mark = "·  ";
        if (i < m_current || (m_finished && m_ok)) mark = m_seen[i] ? "✓  " : "–  ";
        else if (i == m_current) mark = m_finished ? (m_ok ? "✓  " : "✗  ") : "▶  ";
        m_steps->item(i)->setText(mark + m_plan[i]);
    }
}

void BuildWindow::setStep(int index) {
    m_seen[index] = true;
    m_current = index;
    showSteps();
    const int n = int(m_plan.size());
    m_bar->setRange(0, n);
    m_bar->setValue(index);
    m_bar->setFormat(QString("%1 / %2").arg(index).arg(n));
    m_step->setText(QString("Step %1 of %2: %3").arg(index + 1).arg(n).arg(m_plan[index]));
    m_steps->setCurrentRow(index);
}

void BuildWindow::tick() {
    if (!m_start.isValid()) return;
    m_elapsed->setText(m_finished ? "took " + hms(m_start.secsTo(m_last))
                                  : "elapsed " + hms(m_start.secsTo(QDateTime::currentDateTime())));
    if (m_finished) m_clock->stop();
}
