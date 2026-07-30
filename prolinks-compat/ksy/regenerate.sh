#!/usr/bin/env bash
# Regenerate the Kaitai parsers from the .ksy sources in this directory.
#
# Two targets, for two different jobs:
#
#   cpp_stl -> ../../mixxx/src/network/prolink/generated/
#              The parsers Mixxx compiles. Checked into the Mixxx tree, exactly
#              as lib/rekordbox-metadata/rekordbox_pdb.cpp already is, so that
#              building Mixxx never needs a JVM.
#
#   python  -> ../tests/generated/
#              Used by tests/test_ksy_corpus.py to parse the whole capture
#              corpus and diff every field against the hand-written codecs in
#              prolinks_poc/proto/. Three independent implementations of the
#              same spec disagreeing is how we find out which one is wrong; two
#              agreeing is weak evidence, and one is none.
#              Also checked in -- the test importorskips it, so leaving it out
#              would make the suite quietly stop testing anything on a machine
#              without a JVM, which is the worst of the three outcomes.
#
# Kaitai cannot generate C++ *serializers* -- writing is supported only for the
# Java and Python targets, and the runtime vendored at mixxx/lib/kaitai (0.11)
# has no write_* methods at all. So the encode direction is hand-written in
# mixxx/src/network/prolink/wire/, and its unit tests round-trip through these
# generated parsers. See research/10, "Kaitai Struct: what it can and cannot do".
#
# Requires: kaitai-struct-compiler 0.11 (brew install kaitai-struct-compiler).
# The version matters -- the generated headers #error out below 0.11.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_OUT="$HERE/../../mixxx/src/network/prolink/generated"
PY_OUT="$HERE/../tests/generated"

command -v kaitai-struct-compiler >/dev/null || {
    echo "kaitai-struct-compiler not found: brew install kaitai-struct-compiler" >&2
    exit 1
}

want=0.11
have="$(kaitai-struct-compiler --version | awk '{print $2}')"
[ "$have" = "$want" ] || echo "WARNING: ksc $have, expected $want (runtime is 0.11)" >&2

mkdir -p "$CPP_OUT" "$PY_OUT"

# --cpp-standard 11 to match Mixxx's own generated rekordbox parsers, which the
# same runtime compiles. Bumping it here alone would be an inconsistency, not an
# upgrade.
for ksy in "$HERE"/*.ksy; do
    echo "  $(basename "$ksy")"
    kaitai-struct-compiler --target cpp_stl --cpp-standard 11 \
        --outdir "$CPP_OUT" "$ksy" >/dev/null
    kaitai-struct-compiler --target python \
        --outdir "$PY_OUT" "$ksy" >/dev/null
done

# The Python target needs this to be a package for the tests to import it.
touch "$PY_OUT/__init__.py"

echo
echo "C++    -> $CPP_OUT"
echo "Python -> $PY_OUT"
echo
echo "Generated files are checked in. Commit them alongside the .ksy change, or"
echo "the two trees drift and Mixxx silently compiles a stale parser."
