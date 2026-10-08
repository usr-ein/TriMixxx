#pragma once
// pi-qemu's own lines for the person, each written out at once: they
// interleave with the output of the programs it runs (ssh, docker), which go
// straight to the terminal and the build log.
//
//   print() << name << " is up\n";      eprint() << "warning: ...\n";

#include <QTextStream>

#include <cstdio>

class Print {
public:
    explicit Print(FILE* f) : m_s(f) {}
    ~Print() { m_s.flush(); }
    template <typename T> Print& operator<<(const T& v) { m_s << v; return *this; }

private:
    QTextStream m_s;
};

inline Print print() { return Print(stdout); }
inline Print eprint() { return Print(stderr); }
