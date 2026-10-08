#pragma once
// The command line's name, as people type it: written here only, for the
// help and every piece of advice it prints ("pi-qemu deck rm NAME"). Its
// scope is all of TriMixxx's tooling, not just QEMU: it may be renamed
// (trimixxx-cli), which is then this line and CMake's target.

#include <QString>

inline constexpr char kTool[] = "pi-qemu";

// A command to suggest: tool("deck up " + name) is "pi-qemu deck up NAME".
inline QString tool(const QString& words) { return QString(kTool) + " " + words; }
