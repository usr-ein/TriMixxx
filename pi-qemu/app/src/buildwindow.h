#pragma once
// A window on an image build (image/build.sh): its log as it is written, in a
// terminal-like pane with bash highlighting, and a progress bar over the
// build's steps with the time elapsed since it started.
//
//   pi-qemu build-log .cache/build/build.log

#include <QDateTime>
#include <QFile>
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
    void setStep(int index);
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
    int             m_current = -1;
    bool            m_finished = false;
};
