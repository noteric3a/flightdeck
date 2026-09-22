from __future__ import annotations

import hmac
import os
import re
import time
from collections import OrderedDict
from contextlib import asynccontextmanager
from dataclasses import dataclass

import httpx
from fastapi import Depends, FastAPI, Header, HTTPException, Query, Request, Response
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

from .cache import FlightCache
from .details import FlightDetailsClient, details_visible
from .flights import bounding_boxes, filter_flights, sample_flights
from .models import Configuration, ROOT
from .provider import OpenSkyClient, UpstreamError
from .renderer import frame_packet
from .store import ConfigStore, RevisionConflict


@dataclass
class Settings:
    admin_token: str
    device_token: str
    database: str = "data/flightdeck.db"
    provider: str = "demo"
    client_id: str = ""
    client_secret: str = ""
    cache_ttl: float = 30
    stale_seconds: float = 180
    cors_origins: tuple = ()
    aeroapi_key: str = ""
    details_cache_ttl: float = 300
    details_max_per_hour: int = 120

    @classmethod
    def from_env(cls):
        provider = os.getenv("FLIGHT_PROVIDER", "demo")
        client_id = os.getenv("OPENSKY_CLIENT_ID", "")
        return cls(
            admin_token=os.getenv("ADMIN_TOKEN", ""),
            device_token=os.getenv("DEVICE_TOKEN", ""),
            database=os.getenv("DATABASE_PATH", "data/flightdeck.db"),
            provider=provider,
            client_id=client_id,
            client_secret=os.getenv("OPENSKY_CLIENT_SECRET", ""),
            cache_ttl=float(
                os.getenv(
                    "CACHE_TTL_SECONDS",
                    "240" if provider == "opensky" and not client_id else "30",
                )
            ),
            stale_seconds=float(os.getenv("STALE_SECONDS", "180")),
            aeroapi_key=os.getenv("AEROAPI_KEY", ""),
            details_cache_ttl=float(os.getenv("DETAILS_CACHE_TTL_SECONDS", "300")),
            details_max_per_hour=int(os.getenv("AEROAPI_MAX_REQUESTS_PER_HOUR", "120")),
            cors_origins=tuple(
                x.strip().rstrip("/")
                for x in os.getenv("CORS_ORIGINS", "").split(",")
                if x.strip()
            ),
        )

    def validate(self):
        if (
            len(self.admin_token) < 16
            or len(self.device_token) < 16
            or self.admin_token == self.device_token
        ):
            raise ValueError(
                "Set distinct ADMIN_TOKEN and DEVICE_TOKEN values of at least 16 characters. Run scripts/setup_env.py first."
            )
        if self.provider not in ("demo", "opensky"):
            raise ValueError("FLIGHT_PROVIDER must be demo or opensky")
        if bool(self.client_id) != bool(self.client_secret):
            raise ValueError(
                "Set both OpenSky client credentials, or leave both empty for anonymous access"
            )
        if not 10 <= self.cache_ttl <= 3600 or not 0 <= self.stale_seconds <= 600:
            raise ValueError(
                "Cache TTL must be 10–3600 seconds; stale window must be 0–600 seconds"
            )
        if "*" in self.cors_origins:
            raise ValueError("CORS_ORIGINS must contain exact editor origins")
        if (
            not 60 <= self.details_cache_ttl <= 3600
            or not 1 <= self.details_max_per_hour <= 10000
        ):
            raise ValueError(
                "Details cache TTL must be 60–3600 seconds; hourly lookups must be 1–10000"
            )


def create_app(settings: Settings | None = None, transport=None):
    settings = settings or Settings.from_env()

    @asynccontextmanager
    async def lifespan(app):
        settings.validate()
        app.state.store = ConfigStore(settings.database)
        app.state.client = httpx.AsyncClient(
            timeout=httpx.Timeout(6, connect=3),
            transport=transport,
            follow_redirects=False,
        )
        app.state.provider = OpenSkyClient(
            app.state.client, settings.client_id, settings.client_secret
        )
        app.state.cache = FlightCache(settings.cache_ttl, settings.stale_seconds)
        app.state.details = FlightDetailsClient(
            app.state.client,
            settings.aeroapi_key,
            settings.details_cache_ttl,
            settings.details_max_per_hour,
        )
        app.state.sample_epoch = time.time()
        app.state.devices = OrderedDict()
        app.state.started_at = time.monotonic()
        try:
            yield
        finally:
            await app.state.client.aclose()
            app.state.store.close()

    app = FastAPI(title="Flightdeck", version="1.0.0", lifespan=lifespan)
    app.add_middleware(
        CORSMiddleware,
        allow_origins=list(settings.cors_origins),
        allow_methods=["GET", "POST", "PUT"],
        allow_headers=["Authorization", "Content-Type", "If-Match"],
        expose_headers=["ETag"],
        allow_credentials=False,
    )

    @app.middleware("http")
    async def limit_body(request: Request, call_next):
        if request.method in ("POST", "PUT", "PATCH"):
            chunks = []
            size = 0
            async for chunk in request.stream():
                size += len(chunk)
                if size > 131072:
                    return Response("Request is too large", status_code=413)
                chunks.append(chunk)
            request._body = b"".join(chunks)
        response = await call_next(request)
        response.headers["X-Content-Type-Options"] = "nosniff"
        response.headers["Referrer-Policy"] = "no-referrer"
        if request.url.path.startswith("/api/"):
            response.headers["Cache-Control"] = "no-store"
        return response

    def auth(expected, authorization):
        supplied = (
            authorization[7:]
            if authorization and authorization.startswith("Bearer ")
            else ""
        )
        if not hmac.compare_digest(supplied.encode(), expected.encode()):
            raise HTTPException(
                401,
                "Invalid or missing bearer token",
                headers={"WWW-Authenticate": "Bearer"},
            )

    def admin(authorization: str | None = Header(default=None)):
        auth(settings.admin_token, authorization)

    def device(authorization: str | None = Header(default=None)):
        auth(settings.device_token, authorization)

    async def snapshot(config):
        boxes = bounding_boxes(config.filters)
        key = (settings.provider, tuple(boxes))

        async def load():
            if settings.provider == "demo":
                return sample_flights(config.filters, app.state.sample_epoch)
            return await app.state.provider.fetch(boxes)

        try:
            flights, status, age, error = await app.state.cache.get(key, load)
            filtered = filter_flights(flights, config.filters)
            return {
                "flights": [f.model_dump() for f in filtered],
                "source": settings.provider,
                "status": status,
                "cache_age_seconds": round(age, 1),
                "error": error,
                "fetched_count": len(flights),
            }, filtered
        except UpstreamError as exc:
            return {
                "flights": [],
                "source": settings.provider,
                "status": "unavailable",
                "cache_age_seconds": None,
                "error": str(exc),
                "retry_after_seconds": round(exc.retry_after),
                "fetched_count": 0,
            }, []

    async def add_preview_details(result, flights, config, flight_id):
        result["details_status"] = "not_requested"
        if flights and details_visible(config):
            selected = next((f for f in flights if f.icao24 == flight_id), flights[0])
            enriched, result["details_status"] = await app.state.details.enrich(
                selected
            )
            result["flights"] = [
                (enriched if f.icao24 == selected.icao24 else f).model_dump()
                for f in flights
            ]
        return result

    @app.get("/api/health")
    def health():
        return {
            "status": "ok",
            "service": "flightdeck",
            "version": "1.0.0",
            "source": settings.provider,
        }

    @app.get("/api/config", dependencies=[Depends(admin)])
    def get_config(response: Response):
        config, revision = app.state.store.read()
        response.headers["ETag"] = f'"{revision}"'
        return {"config": config, "revision": revision}

    @app.put("/api/config", dependencies=[Depends(admin)])
    def put_config(
        config: Configuration,
        response: Response,
        if_match: str | None = Header(default=None),
    ):
        if not if_match or not re.fullmatch(r'"?\d+"?', if_match):
            raise HTTPException(428, "Supply the current revision in If-Match")
        try:
            revision = app.state.store.write(config, int(if_match.strip('"')))
        except RevisionConflict:
            raise HTTPException(
                409,
                "This display was edited elsewhere. Export your draft, then reconnect to load the latest layout.",
            )
        response.headers["ETag"] = f'"{revision}"'
        return {"config": config, "revision": revision}

    @app.get("/api/flights", dependencies=[Depends(admin)])
    async def flights(
        flight_id: str | None = Query(default=None, pattern=r"^[0-9a-f]{6}$"),
    ):
        config, _ = app.state.store.read()
        result, selected = await snapshot(config)
        return await add_preview_details(result, selected, config, flight_id)

    @app.post("/api/preview", dependencies=[Depends(admin)])
    async def preview(
        config: Configuration,
        flight_id: str | None = Query(default=None, pattern=r"^[0-9a-f]{6}$"),
    ):
        result, selected = await snapshot(config)
        return await add_preview_details(result, selected, config, flight_id)

    @app.get("/api/status", dependencies=[Depends(admin)])
    def status():
        now = time.time()
        devices = [
            dict(id=k, **v, online=now - v["last_seen"] < 20)
            for k, v in list(app.state.devices.items())
        ]
        cache = app.state.cache
        return {
            "source": settings.provider,
            "uptime_seconds": round(time.monotonic() - app.state.started_at),
            "cache": {
                "hits": cache.hits,
                "misses": cache.misses,
                "stale_hits": cache.stale_hits,
                "entries": len(cache.entries),
                "ttl_seconds": cache.ttl,
            },
            "provider_requests": app.state.provider.request_count,
            "remaining_credits": app.state.provider.remaining_credits,
            "flight_details": {
                "configured": bool(settings.aeroapi_key),
                "provider": "aeroapi" if settings.aeroapi_key else None,
                "requests": app.state.details.request_count,
                "ttl_seconds": app.state.details.cache.ttl,
                "max_requests_per_hour": settings.details_max_per_hour,
            },
            "devices": devices,
        }

    @app.get("/api/device/frame", dependencies=[Depends(device)])
    async def device_frame(
        x_device_id: str = Header(
            default="flightdeck-1", pattern=r"^[A-Za-z0-9_-]{1,40}$"
        ),
        x_frame_ack: int = Header(default=0, ge=0, le=4294967295),
    ):
        config, revision = app.state.store.read()
        result, flights = await snapshot(config)
        selected = (
            flights[int(time.time() // config.rotation_seconds) % min(len(flights), 20)]
            if flights
            else None
        )
        details_status = "not_requested"
        if selected and details_visible(config):
            selected, details_status = await app.state.details.enrich(selected)
        payload = frame_packet(
            config, selected, revision, result["status"], settings.provider == "demo"
        )
        devices = app.state.devices
        prior = devices.get(x_device_id, {"frames_sent": 0})
        devices[x_device_id] = {
            "last_seen": time.time(),
            "acknowledged_revision": x_frame_ack,
            "frames_sent": prior["frames_sent"] + 1,
            "sent_revision": revision,
            "source": settings.provider,
            "data_status": result["status"],
            "details_status": details_status,
        }
        devices.move_to_end(x_device_id)
        while len(devices) > 20:
            devices.popitem(last=False)
        return Response(
            payload,
            media_type="application/octet-stream",
            headers={
                "X-Config-Revision": str(revision),
                "X-Data-Status": result["status"],
                "X-Data-Source": settings.provider,
            },
        )

    app.mount("/", StaticFiles(directory=str(ROOT / "dist"), html=True), name="editor")
    return app
