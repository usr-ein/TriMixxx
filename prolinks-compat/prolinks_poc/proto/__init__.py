"""Pure codecs: bytes <-> structures.

Nothing in this package may import from :mod:`prolinks_poc.net` or
:mod:`prolinks_poc.core`. No sockets, no timers, no logging, no mutable state.
That constraint is what makes these modules unit-testable without hardware and
directly transcribable into ``src/network/prolink/`` in Mixxx.
"""
