"""Optional, cached FlightAware enrichment for a single displayed flight."""

from __future__ import annotations

import math
import re
import time
from collections import deque
from datetime import datetime, timezone

import httpx

from .cache import FlightCache
from .models import Flight
from .provider import UpstreamError

AIRCRAFT_NAMES = {
    "B788": "B787-8",
    "B789": "B787-9",
    "B78X": "B787-10",
    "B738": "B737-800",
    "E75L": "E175",
    "E75S": "E175",
}
JOURNEY_FIELDS = {"route", "aircraft", "eta", "progress"}


def timestamp(value):
    if not isinstance(value, str):
        return None
    try:
        dt = datetime.fromisoformat(value.replace("Z", "+00:00"))
        return dt.timestamp() if dt.tzinfo is not None and dt.timestamp() >= 0 else None
    except (ValueError, OverflowError, OSError):
        return None


def airport(value):
    if not isinstance(value, dict):
        return None
    for field in ("code_iata", "code_icao"):
        code = value.get(field)
        if isinstance(code, str) and re.fullmatch(r"[A-Z0-9]{3,4}", code):
            return code
    return None


def parse_details(data, callsign, now):
    """Require one matching, departed, unlanded flight; never guess a schedule."""
    if not isinstance(data, dict) or not isinstance(data.get("flights"), list):
        raise UpstreamError("Flight details response was invalid", 300)
    matches = []
    for row in data["flights"]:
        if not isinstance(row, dict):
            continue
        if callsign not in (
            row.get("ident_icao"),
            row.get("ident"),
            row.get("registration"),
        ):
            continue
        departure = timestamp(row.get("actual_off"))
        if (
            row.get("cancelled")
            or row.get("diverted")
            or row.get("actual_on")
            or departure is None
            or not now - 36 * 3600 <= departure <= now
        ):
            continue
        matches.append((row, departure))
    if len(matches) != 1:
        return {}
    row, departure = matches[0]
    arrival = timestamp(row.get("estimated_on"))
    if arrival is not None and arrival <= departure:
        arrival = None
    aircraft = row.get("aircraft_type")
    if not isinstance(aircraft, str) or not re.fullmatch(r"[A-Z0-9 -]{1,24}", aircraft):
        aircraft = None
    progress = row.get("progress_percent")
    if (
        type(progress) not in (int, float)
        or not math.isfinite(progress)
        or not 0 <= progress <= 100
    ):
        progress = None
    return {
        "departure_airport": airport(row.get("origin")),
        "arrival_airport": airport(row.get("destination")),
        "aircraft_type": AIRCRAFT_NAMES.get(aircraft, aircraft),
        "departure_time": departure,
        "arrival_time": arrival,
        "progress_percent": progress,
        "details_source": "aeroapi",
    }


class FlightDetailsClient:
    def __init__(
        self, client, api_key="", ttl=300, max_per_hour=120, clock=time.monotonic
    ):
        self.client, self.api_key, self.clock = client, api_key, clock
        self.cache = FlightCache(ttl, stale_seconds=0, maxsize=128, clock=clock)
        self.max_per_hour = max_per_hour
        self.requests = deque()
        self.request_count = 0
        self.retry_at = 0

    async def enrich(self, flight: Flight):
        if flight.details_source == "sample":
            return flight, "sample"
        if not self.api_key:
            return flight, "not_configured"
        callsign = flight.callsign.strip().upper()
        if flight.on_ground or not re.fullmatch(r"[A-Z0-9]{2,8}", callsign):
            return flight, "unknown"

        async def load():
            clock = self.clock()
            if clock < self.retry_at:
                raise UpstreamError(
                    "Flight details are temporarily unavailable", self.retry_at - clock
                )
            while self.requests and self.requests[0] <= clock - 3600:
                self.requests.popleft()
            if len(self.requests) >= self.max_per_hour:
                raise UpstreamError(
                    "Flight details hourly lookup limit reached",
                    self.requests[0] + 3600 - clock,
                )
            now = time.time()
            iso = lambda t: datetime.fromtimestamp(t, timezone.utc).isoformat()
            params = {
                "start": iso(now - 48 * 3600),
                "end": iso(now + 60),
                "max_pages": 1,
            }
            if flight.airline_code:
                params["ident_type"] = "designator"
            self.requests.append(clock)
            self.request_count += 1
            try:
                response = await self.client.get(
                    f"https://aeroapi.flightaware.com/aeroapi/flights/{callsign}",
                    headers={"x-apikey": self.api_key},
                    params=params,
                )
                if response.status_code in (401, 403, 429):
                    try:
                        retry = min(
                            3600,
                            max(300, float(response.headers.get("Retry-After", 300))),
                        )
                    except ValueError:
                        retry = 300
                    self.retry_at = self.clock() + retry
                    raise UpstreamError(
                        "Flight details access is temporarily unavailable", retry
                    )
                if response.status_code == 404:
                    return {}
                if response.status_code != 200:
                    raise UpstreamError("Flight details provider is unavailable", 60)
                return parse_details(response.json(), callsign, time.time())
            except (httpx.HTTPError, ValueError):
                raise UpstreamError(
                    "Flight details request could not be completed", 60
                ) from None

        try:
            details, status, _, _ = await self.cache.get(callsign, load)
        except UpstreamError:
            return flight, "unavailable"
        if not details:
            return flight, "unknown"
        return Flight.model_validate({**flight.model_dump(), **details}), status


def details_visible(config):
    return any(e.visible and e.field in JOURNEY_FIELDS for e in config.layout.elements)
