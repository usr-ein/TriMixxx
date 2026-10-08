#pragma once
// A command that cannot go on: its message, for the person, and its exit
// status. The deck code and the pipelines throw it; the command line prints
// "pi-qemu: <message>" and exits with the status.

#include <QString>

struct Failure {
    QString message;
    int     status = 1;
};

[[noreturn]] inline void fail(const QString& message, int status = 1) { throw Failure{message, status}; }
