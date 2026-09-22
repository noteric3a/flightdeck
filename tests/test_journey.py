import json
import subprocess
import time
from datetime import datetime, timezone
from unittest.mock import AsyncMock

import httpx
import pytest
from fastapi.testclient import TestClient

from backend.details import FlightDetailsClient, parse_details
from backend.flights import sample_flights
from backend.journey import journey_state, remaining_text
from backend.main import Settings, create_app
from backend.models import ROOT, Flight, defaults
from backend.renderer import render_pixels, rgb565


def iso(value):
    return datetime.fromtimestamp(value, timezone.utc).isoformat()


def record(now, callsign="UAL247", **changes):
    return {
        "ident": callsign,
        "ident_icao": callsign,
        "origin": {"code_iata": "ORD", "code_icao": "KORD"},
        "destination": {"code_iata": "HND", "code_icao": "RJTT"},
        "aircraft_type": "B789",
        "actual_off": iso(now - 3600),
        "estimated_on": iso(now + 3600),
        "actual_on": None,
        "cancelled": False,
        "progress_percent": 50,
        **changes,
    }


def live_flight(index=0):
    flight = sample_flights(defaults().filters)[index]
    return Flight.model_validate(
        {
            **flight.model_dump(),
            **dict.fromkeys(
                (
                    "departure_airport",
                    "arrival_airport",
                    "aircraft_type",
                    "departure_time",
                    "arrival_time",
                    "progress_percent",
                    "details_source",
                )
            ),
        }
    )


def test_details_match_only_one_current_flight_and_ignore_future_or_landed_trips():
    now = 200000
    current = record(now)
    old = record(now, actual_on=iso(now - 10))
    future = record(now, actual_off=iso(now + 10))
    unrelated = record(now, "DAL1082")
    parsed = parse_details(
        {"flights": [future, old, unrelated, current]}, "UAL247", now
    )
    assert (
        parsed["departure_airport"],
        parsed["arrival_airport"],
        parsed["aircraft_type"],
    ) == ("ORD", "HND", "B787-9")
    assert parse_details({"flights": [current, current]}, "UAL247", now) == {}
    assert parse_details({"flights": [old, future, unrelated]}, "UAL247", now) == {}
    missing = record(
        now,
        estimated_on=None,
        progress_percent=None,
        aircraft_type=None,
        origin=None,
        destination=None,
    )
    parsed = parse_details({"flights": [missing]}, "UAL247", now)
    assert parsed["arrival_time"] is None and parsed["departure_airport"] is None
    assert parsed["aircraft_type"] is None and parsed["progress_percent"] is None


async def test_details_cache_updates_position_without_repeated_billing_and_expires():
    now = time.time()
    clock = [0]
    calls = []

    def handler(request):
        calls.append(request)
        assert request.headers["x-apikey"] == "test-key"
        assert request.url.params["max_pages"] == "1"
        assert request.url.params["ident_type"] == "designator"
        return httpx.Response(200, json={"flights": [record(now)]})

    async with httpx.AsyncClient(transport=httpx.MockTransport(handler)) as http:
        client = FlightDetailsClient(http, "test-key", clock=lambda: clock[0])
        original = live_flight()
        enriched, status = await client.enrich(original)
        assert status == "fresh" and enriched.arrival_airport == "HND"
        updated = original.model_copy(update={"altitude_ft": 36000})
        enriched, status = await client.enrich(updated)
        assert status == "cached" and enriched.altitude_ft == 36000 and len(calls) == 1
        clock[0] = 301
        await client.enrich(updated)
        assert len(calls) == 2


@pytest.mark.parametrize("status", [401, 403, 429])
async def test_details_auth_and_rate_failures_back_off_across_callsigns(status):
    requests = []

    def handler(request):
        requests.append(request)
        return httpx.Response(status, headers={"Retry-After": "600"})

    async with httpx.AsyncClient(transport=httpx.MockTransport(handler)) as http:
        client = FlightDetailsClient(http, "test-key")
        for index in (0, 1, 0):
            original = live_flight(index)
            enriched, state = await client.enrich(original)
            assert enriched == original and state == "unavailable"
        assert len(requests) == 1


async def test_details_missing_key_demo_and_hourly_limit_do_not_make_extra_requests():
    requests = []

    def handler(request):
        requests.append(request)
        return httpx.Response(200, json={"flights": []})

    async with httpx.AsyncClient(transport=httpx.MockTransport(handler)) as http:
        client = FlightDetailsClient(http)
        assert (await client.enrich(live_flight()))[1] == "not_configured"
        client = FlightDetailsClient(http, "test-key", max_per_hour=1)
        assert (await client.enrich(sample_flights(defaults().filters)[0]))[
            1
        ] == "sample"
        assert len(requests) == 0
        assert (await client.enrich(live_flight()))[1] == "unknown"
        assert (await client.enrich(live_flight(1)))[1] == "unavailable"
        assert len(requests) == 1


def test_browser_and_device_journey_rendering_match_for_missing_and_boundary_states():
    cfg = defaults()
    flight = live_flight()
    now = 200000
    cases = [
        {},
        {"departure_time": now + 100, "arrival_time": now + 200},
        {"departure_time": now - 100, "arrival_time": now + 100},
        {"departure_time": now - 200, "arrival_time": now - 100},
        {"departure_time": now + 100, "arrival_time": now - 100},
        {"arrival_time": now + 7260},
        {"progress_percent": 25},
    ]
    flights = [
        flight.model_copy(
            update={
                "departure_airport": "ORD",
                "arrival_airport": "HND",
                "aircraft_type": "E175",
                **case,
            }
        )
        for case in cases
    ]
    script = """
import fs from 'node:fs';
import {renderPixels,rgb565} from './dist/core.mjs';
const v=JSON.parse(fs.readFileSync(0,'utf8'));
const font=JSON.parse(fs.readFileSync('./dist/font.json','utf8'));
process.stdout.write(JSON.stringify(v.flights.map(f=>Buffer.from(rgb565(renderPixels(v.config,f,font,'fresh',v.now))).toString('hex'))));
"""
    out = subprocess.run(
        ["node", "--input-type=module", "-e", script],
        input=json.dumps(
            {
                "config": cfg.model_dump(),
                "flights": [f.model_dump() for f in flights],
                "now": now,
            }
        ),
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=True,
    )
    assert [bytes.fromhex(v) for v in json.loads(out.stdout)] == [
        rgb565(render_pixels(cfg, f, now=now)) for f in flights
    ]
    assert journey_state(flights[0], now) == (None, None)
    assert journey_state(flights[2], now) == (2, 0.5)
    assert remaining_text(flights[5], now) == "2H01M"


def test_preview_enriches_only_selected_flight_and_device_uses_the_same_details(
    tmp_path, monkeypatch
):
    now = time.time()
    monkeypatch.setattr("backend.main.time.time", lambda: now)
    seen = []
    api_records = {
        "UAL247": record(
            now, "UAL247", origin={"code_iata": "SFO"}, destination={"code_iata": "NRT"}
        ),
        "DAL1082": record(
            now,
            "DAL1082",
            origin={"code_iata": "BOS"},
            destination={"code_iata": "LAX"},
            aircraft_type="E75L",
        ),
    }

    def handler(request):
        callsign = request.url.path.rsplit("/", 1)[-1]
        seen.append(callsign)
        return httpx.Response(200, json={"flights": [api_records[callsign]]})

    settings = Settings(
        admin_token="test-admin-only-123",
        device_token="test-device-only-456",
        database=str(tmp_path / "test.db"),
        provider="opensky",
        aeroapi_key="test-key",
    )
    app = create_app(settings, transport=httpx.MockTransport(handler))
    admin = {"Authorization": "Bearer " + settings.admin_token}
    with TestClient(app) as client:
        app.state.provider.fetch = AsyncMock(
            return_value=[live_flight(0), live_flight(1)]
        )
        cfg = client.get("/api/config", headers=admin).json()["config"]
        response = client.post("/api/preview?flight_id=a00002", headers=admin, json=cfg)
        assert response.status_code == 200
        flights = response.json()["flights"]
        assert flights[0]["arrival_airport"] is None
        assert (
            flights[1]["departure_airport"],
            flights[1]["arrival_airport"],
            flights[1]["aircraft_type"],
        ) == ("BOS", "LAX", "E175")
        assert seen == ["DAL1082"]
        client.post("/api/preview?flight_id=a00002", headers=admin, json=cfg)
        assert seen == ["DAL1082"]
        other = client.post(
            "/api/preview?flight_id=a00001", headers=admin, json=cfg
        ).json()["flights"]
        assert (other[0]["departure_airport"], other[0]["arrival_airport"]) == (
            "SFO",
            "NRT",
        )
        assert (
            other[1]["departure_airport"] is None
            and other[1]["arrival_airport"] is None
        )
        assert seen == ["DAL1082", "UAL247"]
        packet = client.get(
            "/api/device/frame",
            headers={"Authorization": "Bearer " + settings.device_token},
        )
        assert packet.status_code == 200
        selected = Flight.model_validate(
            flights[int(now // cfg["rotation_seconds"]) % 2]
        )
        selected = Flight.model_validate(
            {
                **selected.model_dump(),
                **parse_details(
                    {"flights": [api_records[selected.callsign]]},
                    selected.callsign,
                    now,
                ),
            }
        )
        assert packet.content[22:] == rgb565(
            render_pixels(defaults(), selected, now=now)
        )
        count = len(seen)
        for element in cfg["layout"]["elements"]:
            if element["field"] in ("route", "aircraft", "eta", "progress"):
                element["visible"] = False
        client.post("/api/preview?flight_id=a00001", headers=admin, json=cfg)
        assert len(seen) == count
