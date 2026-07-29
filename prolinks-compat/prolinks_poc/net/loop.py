"""A tiny ``selectors``-based reactor with an explicit clock.

This replaces the threads-plus-asyncio design used by the reference Python
implementation. The reason is portability, not taste: an explicit
``poll(now)`` maps onto Qt's event loop directly, where each registered socket
becomes a ``QUdpSocket`` with a ``readyRead`` connection and each timer becomes
a ``QTimer``. Coroutine state has no such mapping.

Everything is single-threaded, so callbacks never race and no locking appears
anywhere in this codebase. A callback that blocks stalls the loop -- which is
exactly the constraint the Qt port lives under too, so it is a feature.
"""

from __future__ import annotations

import heapq
import itertools
import logging
import selectors
import time
from dataclasses import dataclass, field
from typing import Callable, Iterable

log = logging.getLogger(__name__)

__all__ = ["EventLoop", "TimerHandle"]


@dataclass(order=True)
class TimerHandle:
    """A scheduled callback. Compare by (deadline, seq) for heap ordering."""

    deadline: float
    seq: int
    callback: Callable[[], None] = field(compare=False)
    interval: float | None = field(default=None, compare=False)
    cancelled: bool = field(default=False, compare=False)

    def cancel(self) -> None:
        self.cancelled = True


class EventLoop:
    """Single-threaded reactor over UDP sockets and timers.

    Usage::

        loop = EventLoop()
        loop.add_reader(sock, on_readable)
        loop.call_every(1.5, send_keepalive)
        loop.run_for(30.0)
    """

    def __init__(self, clock: Callable[[], float] = time.monotonic) -> None:
        self._clock = clock
        self._selector = selectors.DefaultSelector()
        self._timers: list[TimerHandle] = []
        self._seq = itertools.count()
        self._stopped = False

    # -- clock -----------------------------------------------------------

    def now(self) -> float:
        return self._clock()

    # -- sockets ---------------------------------------------------------

    def add_reader(self, sock, callback: Callable[[], None]) -> None:
        """Call *callback* whenever *sock* becomes readable.

        The callback takes no arguments and is expected to drain the socket
        itself; this mirrors ``QUdpSocket::readyRead``, which is also
        argument-free and level-triggered per pending datagram.
        """
        try:
            self._selector.register(sock, selectors.EVENT_READ, callback)
        except KeyError:
            self._selector.modify(sock, selectors.EVENT_READ, callback)

    def remove_reader(self, sock) -> None:
        try:
            self._selector.unregister(sock)
        except KeyError:
            pass

    def readers(self) -> Iterable:
        return [key.fileobj for key in self._selector.get_map().values()]

    # -- timers ----------------------------------------------------------

    def call_at(self, deadline: float, callback: Callable[[], None]) -> TimerHandle:
        handle = TimerHandle(deadline=deadline, seq=next(self._seq), callback=callback)
        heapq.heappush(self._timers, handle)
        return handle

    def call_later(self, delay: float, callback: Callable[[], None]) -> TimerHandle:
        return self.call_at(self.now() + delay, callback)

    def call_every(self, interval: float, callback: Callable[[], None]) -> TimerHandle:
        """Schedule a repeating callback, first fired one *interval* from now.

        Re-armed relative to the scheduled deadline rather than to completion
        time, so a slow callback does not make the cadence drift. If the loop
        is starved badly enough that several firings are missed, the timer
        catches up to the present instead of firing in a burst.
        """
        handle = TimerHandle(
            deadline=self.now() + interval,
            seq=next(self._seq),
            callback=callback,
            interval=interval,
        )
        heapq.heappush(self._timers, handle)
        return handle

    def _next_deadline(self) -> float | None:
        while self._timers and self._timers[0].cancelled:
            heapq.heappop(self._timers)
        return self._timers[0].deadline if self._timers else None

    def _fire_due_timers(self, now: float) -> int:
        fired = 0
        while self._timers:
            handle = self._timers[0]
            if handle.cancelled:
                heapq.heappop(self._timers)
                continue
            if handle.deadline > now:
                break
            heapq.heappop(self._timers)
            try:
                handle.callback()
            except Exception:
                log.exception("timer callback failed")
            fired += 1
            if handle.interval is not None and not handle.cancelled:
                # Catch up rather than burst if we fell far behind.
                handle.deadline = max(handle.deadline + handle.interval, now)
                handle.seq = next(self._seq)
                heapq.heappush(self._timers, handle)
        return fired

    # -- driving ---------------------------------------------------------

    def poll(self, max_wait: float | None = None) -> int:
        """Run one iteration: wait for I/O, then fire due callbacks.

        Returns the number of callbacks invoked. *max_wait* caps how long the
        selector may block; the actual wait is the smaller of it and the next
        timer deadline.
        """
        now = self.now()
        deadline = self._next_deadline()
        timeout = max_wait
        if deadline is not None:
            until_timer = max(0.0, deadline - now)
            timeout = until_timer if timeout is None else min(timeout, until_timer)

        events = self._selector.select(timeout) if self._selector.get_map() else []
        if not self._selector.get_map() and timeout:
            # No sockets registered; the selector would return instantly and
            # spin the CPU, so honour the timeout ourselves.
            time.sleep(timeout)

        fired = 0
        for key, _mask in events:
            try:
                key.data()
            except Exception:
                log.exception("reader callback failed")
            fired += 1

        return fired + self._fire_due_timers(self.now())

    def run_for(self, duration: float, max_wait: float = 0.25) -> None:
        """Drive the loop for *duration* seconds."""
        self.run_until(deadline=self.now() + duration, max_wait=max_wait)

    def run_until(
        self,
        predicate: Callable[[], bool] | None = None,
        deadline: float | None = None,
        max_wait: float = 0.25,
    ) -> bool:
        """Drive until *predicate* is true, *deadline* passes, or :meth:`stop`.

        Returns ``True`` if the predicate was satisfied, ``False`` if we timed
        out or were stopped -- the distinction callers need in order to report
        "no reply" versus "got it".
        """
        self._stopped = False
        while not self._stopped:
            if predicate is not None and predicate():
                return True
            if deadline is not None:
                remaining = deadline - self.now()
                if remaining <= 0:
                    return False
                self.poll(min(max_wait, remaining))
            else:
                self.poll(max_wait)
        return predicate() if predicate is not None else False

    def stop(self) -> None:
        self._stopped = True

    def close(self) -> None:
        self._selector.close()

    def __enter__(self) -> "EventLoop":
        return self

    def __exit__(self, *exc) -> None:
        self.close()
