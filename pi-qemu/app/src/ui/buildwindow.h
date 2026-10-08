#pragma once
// A window on a build (image build, release build, which both open it): its
// log as it is written, in a terminal-like pane with bash highlighting, and a
// progress bar over the build's steps with the time elapsed since it started.
// The steps are the build's own: its "==> plan:" line (util/buildlog), or,
// without one, its "==>" lines as they come.
//
//   pi-qemu build-log pi-qemu/.cache/build/trimixxx0.log

#include <QDateTime>
#include <QFile>
#include <QVector>
#include <QWidget>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QTimer;

class BuildWindow : public QWidget {
    Q_OBJECT
public:
    explicit BuildWindow(const QString& logPath, QWidget* parent = nullptr);

private:
    void poll();
    void onLine(const QString& line);
    void setPlan(const QStringList& titles);
    void setStep(int index);
    void showSteps();
    void tick();

    QString         m_path;
    QFile           m_file;
    qint64          m_pos = 0;
    QByteArray      m_partial;
    QPlainTextEdit* m_log;
    QProgressBar*   m_bar;
    QLabel*         m_step;
    QLabel*         m_elapsed;
    QListWidget*    m_steps;
    QTimer*         m_clock;
    QDateTime       m_start, m_last; // the build's first and latest stage stamps
    QStringList     m_plan;    // the steps' titles
    bool            m_planned = false; // from a plan line: other "==>" lines are not steps
    int             m_current = -1;
    QVector<bool>   m_seen;    // the steps that started, by index
    bool            m_finished = false, m_ok = false;
};
