from httpx import ASGITransport, AsyncClient

from app.api.v1.health import db_status
from app.main import create_app


async def _get_health(db_ok: bool) -> dict:
    app = create_app()
    app.dependency_overrides[db_status] = lambda: db_ok
    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        resp = await client.get("/api/v1/health")
    assert resp.status_code == 200
    return resp.json()


async def test_health_ok():
    body = await _get_health(True)
    assert body["status"] == "ok"
    assert body["db"] == "ok"


async def test_health_degraded_without_db():
    body = await _get_health(False)
    assert body["status"] == "degraded"
    assert body["db"] == "error"
