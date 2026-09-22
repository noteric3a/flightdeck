import asyncio

import pytest

from backend.cache import FlightCache
from backend.provider import UpstreamError


@pytest.mark.asyncio
async def test_concurrent_refresh_happens_once():
    cache = FlightCache()
    calls = 0

    async def load():
        nonlocal calls
        calls += 1
        await asyncio.sleep(0.01)
        return [1, 2, 3]

    results = await asyncio.gather(*(cache.get("scope", load) for _ in range(20)))
    assert calls == 1 and all(r[0] == [1, 2, 3] for r in results)


@pytest.mark.asyncio
async def test_stale_fallback_expires_and_backoff_is_shared():
    clock = [0]
    cache = FlightCache(ttl=30, stale_seconds=60, clock=lambda: clock[0])

    async def good():
        return ["real flight"]

    calls = 0

    async def bad():
        nonlocal calls
        calls += 1
        raise UpstreamError("Rate limited", 300)

    await cache.get("key", good)
    clock[0] = 40
    stale = await cache.get("key", bad)
    assert stale[0] == ["real flight"] and stale[1] == "stale"
    clock[0] = 50
    await cache.get("key", bad)
    assert calls == 1
    clock[0] = 91
    with pytest.raises(UpstreamError):
        await cache.get("key", bad)
    assert calls == 1


@pytest.mark.asyncio
async def test_slow_failure_does_not_extend_stale_window():
    clock = [0]
    cache = FlightCache(ttl=10, stale_seconds=10, clock=lambda: clock[0])

    async def good():
        return []

    async def bad():
        clock[0] = 30
        raise UpstreamError("Failed")

    await cache.get("a", good)
    clock[0] = 15
    with pytest.raises(UpstreamError):
        await cache.get("a", bad)


@pytest.mark.asyncio
async def test_empty_results_are_cached_and_entries_bounded():
    cache = FlightCache(maxsize=2)

    async def load():
        return []

    await cache.get("a", load)
    assert (await cache.get("a", load))[1] == "cached"
    await cache.get("b", load)
    await cache.get("c", load)
    assert list(cache.entries) == ["b", "c"]
