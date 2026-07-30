"""Put the repository root on ``sys.path`` for the test suite.

``tests/test_ksy_corpus.py`` imports ``tests.generated.prolink_*`` -- the Kaitai
parsers built by ``ksy/regenerate.sh`` -- which only resolves if the root is
importable. Bare ``pytest`` inserts ``tests/`` and not the root, so without this
file the import fails and the whole module is *skipped*: the schemas silently
stop being tested, which is exactly the failure mode that file's own docstring
warns about. ``python -m pytest`` happened to work, so it went unnoticed.

pytest adds the directory holding the topmost conftest.py to ``sys.path`` in the
default import mode, so an empty one here is the entire fix.
"""
