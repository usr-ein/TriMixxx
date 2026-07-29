"""Protocol state and the domain model.

Sits between the pure codecs in :mod:`prolinks_poc.proto` and the I/O in
:mod:`prolinks_poc.net`. State machines here are explicit so they transcribe
into Qt classes rather than needing to be re-derived.
"""
