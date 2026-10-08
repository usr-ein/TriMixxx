#pragma once
// The deck's locked card and its updates over the air (pi-qemu/PLAN.md Part 1).
// The Mac's half: the version, the checks, the system card (image build
// trimixxx-release), and the release container -- release/Dockerfile, running
// release/container.sh, whose tools are Debian trixie's, the decks' own.
//
// A release's version is HEAD's tag pi/vX.Y.Z (the repo's other parts tag
// midi-s3/v..., prolinks-compat/v...): after `git tag -m "TriMixxx 1.0.0"
// pi/v1.0.0` (tags are signed, so they need -m), `release build`, `release
// card` and `deck ship` all mean 1.0.0. Every release built is in
// pi-qemu/release/out/VERSION/ of the main checkout.

#include <QString>
#include <QStringList>

namespace release {

inline constexpr char kTagPrefix[] = "pi/v";

// `given`, or HEAD's pi/v tag; fails, saying how, if there is neither.
QString version(const QString& given);
QString outDir(const QString& version); // pi-qemu/release/out/VERSION

struct BuildOptions {
    QString rehearsal;        // a version for an untagged or dirty tree, said so in its manifest
    bool    keepSystem = false; // seal the system card already built
    bool    window = true;
};
QStringList plan(const BuildOptions& o);
int build(const BuildOptions& o); // the build's exit status (it has a log and a window)

// DECK's card: its identity (pipeline/identity) onto p7 of a copy of the release card.
void card(const QString& deck, const QString& version);
// Broken updates from the release, for the fault tests (container.sh faults).
void faults(const QString& version);

} // namespace release
