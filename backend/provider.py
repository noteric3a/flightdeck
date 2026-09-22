from __future__ import annotations

import asyncio
import math
import time
from email.utils import parsedate_to_datetime

import httpx
from pydantic import ValidationError

from .flights import AIRLINES
from .models import Flight

TOKEN_URL = "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token"
STATES_URL = "https://opensky-network.org/api/states/all"


class UpstreamError(Exception):
    def __init__(self, message, retry_after=5):
        super().__init__(message)
        self.retry_after = retry_after


def retry_delay(headers):
    value = headers.get(
        "X-Rate-Limit-Retry-After-Seconds", headers.get("Retry-After", "60")
    )
    try:
        result = float(value)
        return max(1, result) if math.isfinite(result) else 60
    except (ValueError, TypeError):
        try:
            return max(1, parsedate_to_datetime(value).timestamp() - time.time())
        except (ValueError, TypeError, OverflowError):
            return 60


class OpenSkyClient:
    def __init__(
        self,
        client: httpx.AsyncClient,
        client_id="",
        client_secret="",
        clock=time.monotonic,
    ):
        self.client = client
        self.client_id, self.client_secret = client_id, client_secret
        self.clock = clock
        self.token, self.expires_at = "", 0
        self.retry_at = 0
        self.remaining_credits = None
        self.request_count = 0
        self.token_lock = asyncio.Lock()

    async def headers(self):
        if not self.client_id:
            return {}
        async with self.token_lock:
            if self.token and self.clock() < self.expires_at:
                return {"Authorization": f"Bearer {self.token}"}
            try:
                self.request_count += 1
                r = await self.client.post(
                    TOKEN_URL,
                    data={
                        "grant_type": "client_credentials",
                        "client_id": self.client_id,
                        "client_secret": self.client_secret,
                    },
                )
                if r.status_code == 429:
                    delay = retry_delay(r.headers)
                    self.retry_at = self.clock() + delay
                    raise UpstreamError("OpenSky authentication is rate limited", delay)
                r.raise_for_status()
                body = r.json()
                token = body["access_token"]
                lifetime = float(body.get("expires_in", 1800))
                if (
                    not isinstance(token, str)
                    or not token
                    or not math.isfinite(lifetime)
                    or lifetime <= 0
                ):
                    raise ValueError("Invalid token response")
                self.token = token
                self.expires_at = self.clock() + max(1, lifetime - 30)
            except (httpx.HTTPError, ValueError, KeyError, TypeError) as exc:
                raise UpstreamError(
                    "OpenSky authentication failed; check server credentials", 60
                ) from exc
        return {"Authorization": f"Bearer {self.token}"}

    async def _fetch_box(self, box):
        params = dict(zip(("lamin", "lomin", "lamax", "lomax"), box))
        refreshed = False
        for attempt in range(3):
            try:
                headers = await self.headers()
                self.request_count += 1
                response = await self.client.get(
                    STATES_URL, params=params, headers=headers
                )
                self.remaining_credits = response.headers.get(
                    "X-Rate-Limit-Remaining", self.remaining_credits
                )
                if response.status_code == 401 and self.client_id and not refreshed:
                    self.token = ""
                    refreshed = True
                    continue
                if response.status_code == 429:
                    delay = retry_delay(response.headers)
                    self.retry_at = self.clock() + delay
                    raise UpstreamError(
                        "OpenSky quota reached; waiting before retrying", delay
                    )
                if response.status_code >= 500:
                    if attempt < 2:
                        await asyncio.sleep(0.25 * 2**attempt)
                        continue
                    raise UpstreamError("OpenSky is temporarily unavailable", 15)
                response.raise_for_status()
                payload = response.json()
                if not isinstance(payload, dict) or "states" not in payload:
                    raise ValueError("Missing states")
                rows = payload["states"]
                if rows is None:
                    rows = []
                if not isinstance(rows, list):
                    raise ValueError("Invalid states")
                return rows
            except (httpx.TimeoutException, httpx.NetworkError) as exc:
                if attempt < 2:
                    await asyncio.sleep(0.25 * 2**attempt)
                    continue
                raise UpstreamError("OpenSky request timed out", 15) from exc
            except (httpx.HTTPError, ValueError, TypeError) as exc:
                raise UpstreamError("OpenSky returned an invalid response", 30) from exc
        raise UpstreamError("OpenSky authorization failed", 60)

    async def fetch(self, boxes) -> list[Flight]:
        if self.clock() < self.retry_at:
            raise UpstreamError(
                "OpenSky is in a rate-limit cooldown", self.retry_at - self.clock()
            )
        flights = {}
        for box in boxes:
            for row in await self._fetch_box(box):
                flight = parse_state(row)
                if flight:
                    flights[flight.icao24] = flight
        return list(flights.values())


def parse_state(s):
    if (
        not isinstance(s, list)
        or len(s) < 14
        or s[3] is None
        or s[5] is None
        or s[6] is None
    ):
        return None
    try:
        callsign = (s[1] or "").strip().upper()
        prefix = callsign[:3]
        airline_code = (
            prefix if len(prefix) == 3 and prefix.isascii() and prefix.isalpha() else ""
        )
        alt = s[7] if s[7] is not None else s[13]
        return Flight(
            icao24=s[0],
            callsign=callsign,
            airline_code=airline_code,
            airline_name=AIRLINES.get(airline_code, airline_code or "UNKNOWN"),
            latitude=s[6],
            longitude=s[5],
            altitude_ft=None if alt is None else alt / 0.3048,
            speed_knots=None if s[9] is None else s[9] / 0.514444,
            heading=s[10],
            vertical_rate_fpm=None if s[11] is None else s[11] / 0.00508,
            on_ground=s[8],
            position_time=s[3],
            last_contact=s[4],
            origin_country=s[2] or "Unknown",
        )
    except (ValidationError, ValueError, TypeError, AttributeError):
        return None
