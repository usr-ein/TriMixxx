#include "util/meter.h"

#include "util/diskuse.h"

#include <cstdio>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

constexpr qint64 kRedrawMs = 1000;  // a terminal's line, drawn again at most once a second
constexpr qint64 kPlainMs = 30000;  // elsewhere, a line at least every 30 s
constexpr qint64 kWindowMs = 10000; // the rate, over about the last 10 s

// A terminal, which takes a line drawn over itself.
bool terminal() {
    static const bool yes = ::isatty(STDOUT_FILENO) && qgetenv("TERM") != "dumb";
    return yes;
}

int columns() {
    winsize w{};
    return ::ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0 ? w.ws_col : 80;
}

void out(FILE* f, const QString& text) {
    fputs(text.toUtf8().constData(), f);
    fflush(f);
}

// "45 s", "6 min 10 s", "25 min", "1 h 5 min"
QString duration(qint64 s) {
    if (s < 60) return QString("%1 s").arg(s);
    if (s < 600) return QString("%1 min %2 s").arg(s / 60).arg(s % 60);
    if (s < 3600) return QString("%1 min").arg(s / 60);
    return QString("%1 h %2 min").arg(s / 3600).arg(s / 60 % 60);
}

// "350 KB/s", "2.1 MB/s", "112 MB/s"
QString rate(double perSecond) {
    if (perSecond >= 100e6) return QString::number(qRound64(perSecond / 1e6)) + " MB/s";
    if (perSecond >= 1e6) return QString::number(perSecond / 1e6, 'f', 1) + " MB/s";
    return QString::number(qRound64(perSecond / 1e3)) + " KB/s";
}

} // namespace

Meter::Meter(qint64 total, Unit unit) : m_total(qMax<qint64>(total, 1)), m_unit(unit) { m_clock.start(); }

Meter::~Meter() {
    if (m_drawn) out(stdout, "\n");
}

void Meter::update(qint64 done, const QString& note) {
    if (m_finished) return;
    m_started = true;
    m_done = qBound<qint64>(0, done, m_total);
    if (!note.isEmpty()) m_note = note;
    show();
}

void Meter::tick() {
    if (m_started && !m_finished) show();
}

void Meter::say(const QString& line, bool err) {
    if (m_drawn) out(stdout, "\r\033[K");
    out(err ? stderr : stdout, line + "\n");
    if (m_drawn) draw(m_clock.elapsed(), false);
}

QString Meter::sofar() const {
    const int percent = int(m_done * 100 / m_total);
    if (m_unit == Percent) return QString("%1%").arg(percent);
    return QString("%1 of %2 (%3%)").arg(diskuse::text(m_done), diskuse::text(m_total)).arg(percent);
}

void Meter::show() {
    const qint64 now = m_clock.elapsed();
    if (m_samples.empty() || now - m_samples.back().ms >= 1000) m_samples.push_back({now, m_done});
    while (m_samples.size() > 2 && now - m_samples.front().ms > kWindowMs) m_samples.pop_front();
    const bool final = m_done == m_total;
    if (terminal()) {
        if (final || !m_drawn || now - m_drawnAt >= kRedrawMs) draw(now, final);
    } else {
        const int tenth = int(m_done * 10 / m_total);
        if (final || tenth > m_tenth || now - m_lineAt >= kPlainMs) {
            out(stdout, figures(now, final) + "\n");
            m_tenth = tenth;
            m_lineAt = now;
        }
    }
    m_finished = final;
}

// The bar takes what the figures leave of the line (50 columns kept for them,
// so it keeps its width as they change), 30 columns at most; with fewer than
// 10 left, only the figures. Never the last column: the terminal would wrap
// the line, and \r goes back to the start of the wrapped part only.
void Meter::draw(qint64 now, bool final) {
    const QString f = figures(now, final);
    const int room = columns() - 1;
    const int width = qMin(30, room - qMax(int(f.size()), 50) - 4);
    QString line = f.left(room);
    if (width >= 10) {
        const int filled = int(m_done * width / m_total);
        QString bar = QString(filled, u'=') + QString(width - filled, u' ');
        if (filled > 0 && filled < width) bar[filled - 1] = u'>';
        line = "[" + bar + "]  " + f;
    }
    out(stdout, "\r" + line + "\033[K" + (final ? "\n" : ""));
    m_drawn = !final;
    m_drawnAt = now;
}

QString Meter::figures(qint64 now, bool final) const {
    QString f = QString("%1%").arg(int(m_done * 100 / m_total), 3);
    if (m_unit == Percent) return f + (m_note.isEmpty() ? "" : "  " + m_note) + "  (" + duration(now / 1000) + ")";
    if (final)
        return f + "  " + diskuse::text(m_total) + " in " + duration(now / 1000) + ", " +
               rate(m_total * 1000.0 / qMax<qint64>(now, 1));
    f += "  " + diskuse::text(m_done) + " of " + diskuse::text(m_total);
    // The rate over the samples' window, and the time left at that rate.
    if (!m_samples.empty() && now - m_samples.front().ms >= 1000) {
        const double r = (m_done - m_samples.front().done) * 1000.0 / (now - m_samples.front().ms);
        f += "  " + rate(r);
        if (r > 0) f += "  " + duration(qint64((m_total - m_done) / r)) + " left";
    }
    return f;
}
