#pragma once
// How far a long step has got, for the person waiting on it (deck ship's
// transfer and install). When pi-qemu's output is a terminal: one line, a bar
// and its figures, drawn again about once a second. Anywhere else (a pipe, a
// log, an agent): a plain line with the same figures every tenth of the way,
// or every 30 s. What the step's programs print goes through say(), onto
// lines of its own rather than into the bar.
//
//   Meter m(size, Meter::Bytes);  [=========>          ]  31%  352 MB of 1.1 GB  2.1 MB/s  6 min left
//   Meter m(100, Meter::Percent); [=============>      ]  45%  Copying image to rootfs.1  (1 min 2 s)
//
// Nothing shows until the first update(); at its total the line is final.

#include <QElapsedTimer>
#include <QString>

#include <deque>

class Meter {
public:
    enum Unit { Bytes, Percent };

    Meter(qint64 total, Unit unit);
    ~Meter(); // cut short (a failure): the bar stays as it was, on its own line

    // How far it has got, and in the step's own words if it has them (RAUC's).
    void update(qint64 done, const QString& note = {});
    // No news: the figures again (the time, the rate), when they are due.
    void tick();
    // A program's line, out of the bar's way; stderr's with `err`.
    void say(const QString& line, bool err = false);
    // "352 MB of 1.1 GB (31%)", "45%": how far it got, for a failure's message.
    QString sofar() const;

private:
    struct Sample {
        qint64 ms;
        qint64 done;
    };

    void    show();
    void    draw(qint64 now, bool final);
    QString figures(qint64 now, bool final) const;

    const qint64       m_total;
    const Unit         m_unit;
    QElapsedTimer      m_clock;
    qint64             m_done = 0;
    QString            m_note;
    bool               m_started = false;
    bool               m_finished = false;
    std::deque<Sample> m_samples; // one a second, the last ~10 s: the rate
    bool               m_drawn = false; // a terminal: the bar is on the line the cursor is on
    qint64             m_drawnAt = 0;
    int                m_tenth = 0;     // elsewhere: the last plain line's tenth of the way
    qint64             m_lineAt = 0;    // and when it was printed
};
