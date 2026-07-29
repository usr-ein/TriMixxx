"""prolinks-compat proof-of-concept ProLink client.

Written to be *ported* to Mixxx (C++/Qt), not to be idiomatic Python. See
``research/10-mixxx-prolink-implementation-plan.md`` for the portability rules
that constrain this package:

* explicit ``struct`` codecs at named constant offsets (no declarative parsers)
* one single-threaded ``selectors`` reactor with an explicit ``poll(now)``
* explicit ``enum`` state machines
* stdlib only below :mod:`prolinks_poc.cli`
* every protocol module encodes **and** decodes **both** directions, so the
  serve side (objective 2) is plumbing rather than a rewrite

Licensing: this package is GPLv2-or-later, matching Mixxx. Protocol *facts*
come from the ``research/*.md`` documents; no code is copied from the
Apache-2.0 / EPL / unlicensed reference repositories. See ``PROVENANCE.md``.
"""

__version__ = "0.1.0"
