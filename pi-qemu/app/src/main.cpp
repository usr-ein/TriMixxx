// pi-qemu: TriMixxx decks, emulated and real. `pi-qemu help` lists what it
// does; cli/commands/ has each group's verbs.
//
// A GUI app only when the call shows a window (`run` with its control panel,
// `build-log`): everything else -- every other verb, a headless board, a build
// -- is a plain QCoreApplication, with no Dock icon.

#include "cli/cli.h"
#include "cli/commands/commands.h"
#include "util/process.h"
#include "util/tool.h"

#include <QApplication>
#include <QIcon>

#include <memory>

int main(int argc, char** argv) {
    QStringList words;
    for (int i = 1; i < argc; i++) words << QString::fromLocal8Bit(argv[i]);
    cli::Registry registry;
    commands::addAll(registry);
    cli::Registry::Call call = registry.parse(words);

    std::unique_ptr<QCoreApplication> app;
    if (call.verb && call.verb->window && call.verb->window(call.args)) {
        app = std::make_unique<QApplication>(argc, argv);
        QApplication::setWindowIcon(QIcon(":/trimixxx.ico")); // its windows', and its Dock tile
    } else {
        app = std::make_unique<QCoreApplication>(argc, argv);
        if (call.verb && call.verb->name != "run") proc::handleInterrupt();
    }
    QCoreApplication::setApplicationName(kTool);
    return registry.run(call);
}
