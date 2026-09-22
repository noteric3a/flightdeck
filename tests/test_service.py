import struct
import zlib

import httpx
import pytest
from fastapi.testclient import TestClient

from backend.main import Settings, create_app
from backend.models import defaults
from conftest import ADMIN, DEVICE


def test_health_and_gui(client):
    assert client.get("/api/health").json()["service"] == "flightdeck"
    page = client.get("/")
    assert page.status_code == 200 and "Display studio" in page.text
    for asset in (
        "app.mjs",
        "core.mjs",
        "font.json",
        "defaults.json",
        "style.css",
        "validate.mjs",
        "presets.mjs",
        "logos/UAL.png",
        "logos/DAL.png",
        "logos/AAL.png",
        "logos/SWA.png",
        "airline-pixel-logos.zip",
    ):
        assert client.get("/" + asset).status_code == 200
    assert client.get("/.env").status_code == 404
    assert client.get("/backend/main.py").status_code == 404


def test_auth_roles(client, admin, device):
    assert client.get("/api/config").status_code == 401
    assert client.get("/api/config", headers=device).status_code == 401
    assert client.get("/api/device/frame", headers=admin).status_code == 401
    assert client.get("/api/device/frame").status_code == 401
    assert client.post("/api/preview", json=defaults().model_dump()).status_code == 401


def test_edit_layout_and_receive_same_revision(client, admin, device):
    existing = client.get("/api/config", headers=admin).json()
    cfg = existing["config"]
    cfg["layout"]["elements"][0]["x"] = 34
    cfg["layout"]["elements"][4]["visible"] = False
    saved = client.put(
        "/api/config",
        headers={**admin, "If-Match": str(existing["revision"])},
        json=cfg,
    )
    assert saved.status_code == 200
    revision = saved.json()["revision"]
    response = client.get(
        "/api/device/frame",
        headers={**device, "X-Device-ID": "test-panel", "X-Frame-Ack": str(revision)},
    )
    header = struct.unpack("<4sHHBBIII", response.content[:22])
    assert header[:3] == (b"FLT1", 64, 32)
    assert header[5] == revision
    assert header[6] == 4096 and len(response.content) == 4118
    assert header[7] == zlib.crc32(response.content[22:])
    assert header[4] & 8  # Demo flag is honest even on physical hardware.
    assert response.headers["Cache-Control"] == "no-store"
    status = client.get("/api/status", headers=admin).json()
    assert status["devices"][0]["acknowledged_revision"] == revision
    assert status["devices"][0]["online"] is True


def test_optimistic_concurrency(client, admin):
    cfg = defaults().model_dump()
    assert client.put("/api/config", headers=admin, json=cfg).status_code == 428
    assert (
        client.put(
            "/api/config", headers={**admin, "If-Match": "1"}, json=cfg
        ).status_code
        == 200
    )
    assert (
        client.put(
            "/api/config", headers={**admin, "If-Match": "1"}, json=cfg
        ).status_code
        == 409
    )
    assert client.get("/api/config", headers=admin).json()["revision"] == 2


@pytest.mark.parametrize(
    "modify",
    [
        lambda c: c["layout"]["elements"][0].update(x=60),
        lambda c: c["layout"].update(width=128),
        lambda c: c["layout"]["elements"][1].update(id="logo"),
        lambda c: c["filters"].update(min_altitude_ft=45000, max_altitude_ft=20000),
        lambda c: c["filters"].update(latitude=91),
        lambda c: c["filters"].update(radius_km=9999),
        lambda c: c["layout"]["elements"][0].update(color="javascript:alert(1)"),
        lambda c: c.update(logos={"UAL": {"width": 12, "height": 12, "pixels": []}}),
        lambda c: c.update(
            logos={"UAL": {"width": 28, "height": 28, "pixels": [None] * 144}}
        ),
        lambda c: c.update(
            logos={"UAL": {"width": 33, "height": 1, "pixels": [None] * 33}}
        ),
        lambda c: c.update(
            logos={
                f"AA{chr(65 + i)}": {"width": 32, "height": 32, "pixels": [None] * 1024}
                for i in range(9)
            }
        ),
        lambda c: c.update(schema_version=2),
    ],
)
def test_invalid_config_rejected(client, admin, modify):
    cfg = defaults().model_dump()
    modify(cfg)
    assert (
        client.put(
            "/api/config", headers={**admin, "If-Match": "1"}, json=cfg
        ).status_code
        == 422
    )
    assert client.get("/api/config", headers=admin).json()["revision"] == 1


def test_preview_does_not_change_device_settings(client, admin):
    cfg = defaults().model_dump()
    cfg["filters"]["airlines"] = ["DAL"]
    data = client.post("/api/preview", headers=admin, json=cfg).json()
    assert len(data["flights"]) == 1 and data["flights"][0]["callsign"] == "DAL1082"
    assert (
        client.get("/api/config", headers=admin).json()["config"]["filters"]["airlines"]
        == []
    )


def test_empty_flights_render_valid_idle_packet(client, admin, device):
    cfg = defaults().model_dump()
    cfg["filters"]["airlines"] = ["ZZZ"]
    client.put("/api/config", headers={**admin, "If-Match": "1"}, json=cfg)
    assert client.get("/api/flights", headers=admin).json()["flights"] == []
    packet = client.get("/api/device/frame", headers=device).content
    assert packet[9] & 2 and any(packet[22:])


def test_cache_shared_between_gui_and_device(client, admin, device):
    client.get("/api/flights", headers=admin)
    for _ in range(4):
        client.get("/api/device/frame", headers=device)
    stats = client.get("/api/status", headers=admin).json()["cache"]
    assert stats["misses"] == 1 and stats["hits"] == 4


def test_cors_uses_explicit_origin(client):
    headers = {
        "Origin": "https://editor.example",
        "Access-Control-Request-Method": "PUT",
        "Access-Control-Request-Headers": "authorization,content-type,if-match",
    }
    result = client.options("/api/config", headers=headers)
    assert result.headers["access-control-allow-origin"] == "https://editor.example"
    headers["Origin"] = "https://untrusted.example"
    assert (
        "access-control-allow-origin"
        not in client.options("/api/config", headers=headers).headers
    )


def test_payload_limit_and_device_id_validation(client, admin, device):
    assert (
        client.post("/api/preview", headers=admin, content=b"x" * 131073).status_code
        == 413
    )
    assert (
        client.get(
            "/api/device/frame", headers={**device, "X-Device-ID": "x" * 41}
        ).status_code
        == 422
    )


def test_outage_never_becomes_sample_data(tmp_path):
    app = create_app(
        Settings(ADMIN, DEVICE, database=str(tmp_path / "test.db"), provider="opensky"),
        httpx.MockTransport(
            lambda r: httpx.Response(
                429, headers={"X-Rate-Limit-Retry-After-Seconds": "3600"}
            )
        ),
    )
    with TestClient(app) as client:
        data = client.get(
            "/api/flights", headers={"Authorization": "Bearer " + ADMIN}
        ).json()
        assert (
            data["status"] == "unavailable"
            and data["source"] == "opensky"
            and data["flights"] == []
        )
        packet = client.get(
            "/api/device/frame", headers={"Authorization": "Bearer " + DEVICE}
        ).content
        assert packet[9] & 4 and not packet[9] & 8
        assert app.state.provider.request_count == 1


def test_configuration_survives_service_restart(tmp_path, admin):
    settings = Settings(ADMIN, DEVICE, database=str(tmp_path / "restart.db"))
    cfg = defaults().model_dump()
    cfg["layout"]["brightness"] = 55
    transport = httpx.MockTransport(lambda r: httpx.Response(503))
    with TestClient(create_app(settings, transport)) as client:
        assert (
            client.put(
                "/api/config", headers={**admin, "If-Match": "1"}, json=cfg
            ).status_code
            == 200
        )
    with TestClient(create_app(settings, transport)) as client:
        assert (
            client.get("/api/config", headers=admin).json()["config"]["layout"][
                "brightness"
            ]
            == 55
        )
