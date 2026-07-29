"""JSONL journal of every datagram in and out.

More useful than a pcap, because each line carries *our interpretation*
alongside the bytes: if a decoder is wrong, the journal shows both what
arrived and what we made of it. It is also the input to the golden-decode
generator and to ``spec/gen_spec.py``.

One line per datagram::

    {"ts_mono": 12.345, "ts_utc": "2026-07-29T18:22:01.123456+00:00",
     "dir": "rx", "local_port": 50000, "peer_ip": "169.254.119.181",
     "peer_port": 50000, "len": 54, "hex": "5173...",
     "decoded": {...} | null, "decode_error": null}

``ts_mono`` is the monotonic clock, so inter-packet timing survives even if
the wall clock steps; ``ts_utc`` is what a human correlates with a pcap.
"""

from __future__ import annotations

import json
import time
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

__all__ = ["Recorder", "journal_entries"]


class Recorder:
    """Append-only journal writer plus in-memory transmission counters.

    Constructing with ``directory=None`` disables file output but keeps the
    counters, so ``--no-record`` still supports the passivity assertion.
    """

    def __init__(self, directory: Path | None, clock=time.monotonic) -> None:
        self.directory = Path(directory) if directory is not None else None
        self._clock = clock
        self._start = clock()
        self._file = None
        self.counts: Counter[str] = Counter()

        if self.directory is not None:
            self.directory.mkdir(parents=True, exist_ok=True)
            self._file = (self.directory / "journal.jsonl").open("a", encoding="utf-8")

    # -- recording -------------------------------------------------------

    def record(
        self,
        direction: str,
        local_port: int,
        peer: tuple[str, int],
        data: bytes,
        decoded: Any = None,
        decode_error: str | None = None,
    ) -> None:
        key = f"{direction}:{local_port}"
        self.counts[key] += 1
        self.counts[direction] += 1

        if self._file is None:
            return

        entry = {
            "ts_mono": round(self._clock() - self._start, 6),
            "ts_utc": datetime.now(timezone.utc).isoformat(),
            "dir": direction,
            "local_port": local_port,
            "peer_ip": peer[0] if peer else None,
            "peer_port": peer[1] if peer else None,
            "len": len(data),
            "hex": data.hex(),
            "decoded": decoded,
            "decode_error": decode_error,
        }
        self._file.write(json.dumps(entry, sort_keys=True) + "\n")
        self._file.flush()

    def note(self, event: str, **fields: Any) -> None:
        """Record a non-datagram event (command start, hardware state, verdict).

        These are what turn a journal into something still readable in six
        months, so ``cli`` writes the exact invocation and the operator's
        ``--notes`` through here.
        """
        if self._file is None:
            return
        entry = {
            "ts_mono": round(self._clock() - self._start, 6),
            "ts_utc": datetime.now(timezone.utc).isoformat(),
            "dir": "note",
            "event": event,
            **fields,
        }
        self._file.write(json.dumps(entry, sort_keys=True, default=str) + "\n")
        self._file.flush()

    # -- passivity evidence ----------------------------------------------

    def transmitted_on(self, ports: frozenset[int]) -> dict[int, int]:
        """Datagrams we sent from each of *ports*. Empty dict means passive."""
        return {
            port: self.counts[f"tx:{port}"]
            for port in sorted(ports)
            if self.counts[f"tx:{port}"]
        }

    @property
    def tx_total(self) -> int:
        return self.counts["tx"]

    @property
    def rx_total(self) -> int:
        return self.counts["rx"]

    def close(self) -> None:
        if self._file is not None:
            self._file.close()
            self._file = None

    def __enter__(self) -> "Recorder":
        return self

    def __exit__(self, *exc) -> None:
        self.close()


def journal_entries(path: Path):
    """Yield decoded journal lines, skipping blanks. Used by ``replay``."""
    with Path(path).open(encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line:
                yield json.loads(line)
