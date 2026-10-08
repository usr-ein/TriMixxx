#pragma once
// A build's log and its window. Everything printed from the start of a build
// -- pi-qemu's own lines and every program it runs -- goes to the terminal and
// into the log (a tee, like bash's `exec > >(tee -a LOG)`), and the log ends
// with BUILD_EXIT <status>. `pi-qemu build-log LOG` shows it in a window
// (ui/buildwindow): the build's plan, the step it is on, the time.
//
// The window reads two kinds of line:
//   ==> plan: TITLE | TITLE | ...   the steps this build may take, first
//   ==> [HH:MM:SS] TEXT             a step starting: the planned step TEXT
//                                   begins with (a deploy step: "deploy 004_mixxx")

#include <QString>
#include <QStringList>

#include <functional>

namespace buildlog {

// "\n==> [HH:MM:SS] text": a step's line, which the window follows.
void say(const QString& text);

// Runs `build` with its output also in `logPath` (emptied first), opening the
// window on it unless `window` is false or one is open already. The plan
// comes first in the log; then the build; then BUILD_EXIT and its status,
// which this returns (a Failure's message is printed into the log).
int run(const QString& logPath, bool window, const QStringList& plan, const std::function<void()>& build);

} // namespace buildlog
