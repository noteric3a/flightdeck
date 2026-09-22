import httpx
import pytest
from fastapi.testclient import TestClient

from backend.main import Settings, create_app

ADMIN = "test-admin-token-only-123"
DEVICE = "test-device-token-only-456"


@pytest.fixture
def client(tmp_path):
    settings = Settings(
        admin_token=ADMIN,
        device_token=DEVICE,
        database=str(tmp_path / "settings.db"),
        cors_origins=("https://editor.example",),
    )
    app = create_app(
        settings, transport=httpx.MockTransport(lambda request: httpx.Response(503))
    )
    with TestClient(app) as c:
        yield c


@pytest.fixture
def admin():
    return {"Authorization": "Bearer " + ADMIN}


@pytest.fixture
def device():
    return {"Authorization": "Bearer " + DEVICE}
