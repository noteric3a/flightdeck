from __future__ import annotations

import asyncio
import time
from collections import OrderedDict
from dataclasses import dataclass

from .provider import UpstreamError


@dataclass
class Entry:
    value: object
    created_at: float


class FlightCache:
    """Bounded TTL cache. One refresh at a time; explicit, time-bounded stale fallback."""

    def __init__(self, ttl=30, stale_seconds=180, maxsize=8, clock=time.monotonic):
        self.ttl, self.stale_seconds, self.maxsize, self.clock = (
            ttl,
            stale_seconds,
            maxsize,
            clock,
        )
        self.entries = OrderedDict()
        self.failures = OrderedDict()
        self.lock = asyncio.Lock()
        self.hits = self.misses = self.stale_hits = 0

    async def get(self, key, loader):
        async with self.lock:
            now = self.clock()
            entry = self.entries.get(key)
            age = max(0, now - entry.created_at) if entry else None
            if entry and age < self.ttl:
                self.entries.move_to_end(key)
                self.hits += 1
                return entry.value, "cached", age, None
            failure = self.failures.get(key)
            try:
                if failure and now < failure[0]:
                    raise UpstreamError(failure[1], failure[0] - now)
                self.misses += 1
                value = await loader()
                self.entries[key] = Entry(value, self.clock())
                self.entries.move_to_end(key)
                while len(self.entries) > self.maxsize:
                    self.entries.popitem(last=False)
                self.failures.pop(key, None)
                return value, "fresh", 0, None
            except UpstreamError as exc:
                self.failures[key] = (self.clock() + max(1, exc.retry_after), str(exc))
                self.failures.move_to_end(key)
                while len(self.failures) > self.maxsize:
                    self.failures.popitem(last=False)
                # Recompute age after a slow request: never extend the stale window.
                age = max(0, self.clock() - entry.created_at) if entry else None
                if entry and age <= self.ttl + self.stale_seconds:
                    self.stale_hits += 1
                    return entry.value, "stale", age, str(exc)
                raise
