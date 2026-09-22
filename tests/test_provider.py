import time

import httpx
import pytest

from backend.provider import OpenSkyClient, UpstreamError, parse_state


def state(**changes):
    row = [
        "a12345",
        "UAL247  ",
        "United States",
        time.time(),
        time.time(),
        -86.9,
        40.4,
        10668,
        False,
        231.5,
        78,
        0,
        None,
        10680,
    ]
    for index, value in changes.items():
        row[int(index)] = value
    return row


def test_parse_units_nulls_and_malformed_rows():
    flight = parse_state(state())
    assert flight.callsign == "UAL247" and flight.airline_code == "UAL"
    assert flight.altitude_ft == pytest.approx(35000)
    assert flight.speed_knots == pytest.approx(450, rel=0.01)
    row = state()
    row[7] = None
    row[13] = 0
    row[9] = None
    assert parse_state(row).altitude_ft == 0 and parse_state(row).speed_knots is None
    for row in (
        [],
        None,
        state(**{"5": None}),
        state(**{"3": None}),
        state(**{"6": float("nan")}),
    ):
        assert parse_state(row) is None


@pytest.mark.asyncio
async def test_oauth_refresh_and_401_retry():
    seen = []
    token_calls = 0

    def handler(request):
        nonlocal token_calls
        seen.append(request)
        if request.url.path.endswith("/token"):
            token_calls += 1
            assert b"grant_type=client_credentials" in request.content
            return httpx.Response(
                200, json={"access_token": f"token{token_calls}", "expires_in": 1800}
            )
        if request.headers.get("Authorization") == "Bearer token1":
            return httpx.Response(401)
        assert request.headers["Authorization"] == "Bearer token2"
        return httpx.Response(
            200, json={"states": [state()]}, headers={"X-Rate-Limit-Remaining": "3998"}
        )

    async with httpx.AsyncClient(transport=httpx.MockTransport(handler)) as http:
        provider = OpenSkyClient(http, "id", "secret")
        assert len(await provider.fetch([(40, -87, 41, -86)])) == 1
        await provider.fetch([(40, -87, 41, -86)])
        assert token_calls == 2 and provider.remaining_credits == "3998"
        assert seen[-1].url.params["lamin"] == "40"


@pytest.mark.asyncio
async def test_rate_limit_cooldown_prevents_further_requests():
    calls = 0

    def handler(request):
        nonlocal calls
        calls += 1
        return httpx.Response(429, headers={"X-Rate-Limit-Retry-After-Seconds": "3600"})

    async with httpx.AsyncClient(transport=httpx.MockTransport(handler)) as http:
        provider = OpenSkyClient(http)
        for _ in range(2):
            with pytest.raises(UpstreamError) as exc:
                await provider.fetch([(40, -87, 41, -86)])
            assert exc.value.retry_after > 3500
        assert calls == 1


@pytest.mark.asyncio
async def test_retry_transient_failure_and_deduplicate_boxes():
    calls = 0

    def handler(request):
        nonlocal calls
        calls += 1
        if calls == 1:
            return httpx.Response(503)
        return httpx.Response(200, json={"states": [state()]})

    async with httpx.AsyncClient(transport=httpx.MockTransport(handler)) as http:
        provider = OpenSkyClient(http)
        result = await provider.fetch([(0, 179, 1, 180), (0, -180, 1, -179)])
        assert len(result) == 1 and calls == 3


@pytest.mark.asyncio
async def test_missing_states_is_error_not_empty_airspace():
    async with httpx.AsyncClient(
        transport=httpx.MockTransport(lambda r: httpx.Response(200, json={}))
    ) as http:
        with pytest.raises(UpstreamError):
            await OpenSkyClient(http).fetch([(0, 0, 1, 1)])
