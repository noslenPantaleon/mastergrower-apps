"""Fixtures: API con SQLite en memoria y almacén de imágenes en una carpeta temporal."""

import io
import json
from collections.abc import AsyncIterator

import pytest
from httpx import ASGITransport, AsyncClient
from PIL import Image
from sqlalchemy.ext.asyncio import AsyncSession, async_sessionmaker, create_async_engine
from sqlalchemy.pool import StaticPool

from app.core.config import Settings, get_settings
from app.core.db import get_session
from app.main import create_app
from app.models import Base
from app.storage.image_store import LocalImageStore, get_image_store

TOKEN_CAM = "tok-cam-01"
TOKEN_CAM2 = "tok-cam-02"
TOKEN_USER = "tok-user"
USER = {"Authorization": f"Bearer {TOKEN_USER}"}


@pytest.fixture
def settings(tmp_path) -> Settings:
    return Settings(
        api_node_tokens=f"cam-01:{TOKEN_CAM},cam-02:{TOKEN_CAM2}",
        api_user_tokens=f"dev:{TOKEN_USER}",
        images_dir=str(tmp_path / "img"),
        max_image_bytes=200_000,
    )


@pytest.fixture
async def maker() -> AsyncIterator[async_sessionmaker[AsyncSession]]:
    engine = create_async_engine(
        "sqlite+aiosqlite://", poolclass=StaticPool, connect_args={"check_same_thread": False}
    )
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
    yield async_sessionmaker(engine, expire_on_commit=False)
    await engine.dispose()


@pytest.fixture
async def db(maker) -> AsyncIterator[AsyncSession]:
    async with maker() as s:
        yield s


@pytest.fixture
async def client(settings, maker) -> AsyncIterator[AsyncClient]:
    async def _session() -> AsyncIterator[AsyncSession]:
        async with maker() as s:
            yield s

    store = LocalImageStore(settings.images_dir)
    app = create_app()
    app.dependency_overrides[get_settings] = lambda: settings
    app.dependency_overrides[get_session] = _session
    app.dependency_overrides[get_image_store] = lambda: store
    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as c:
        yield c


def jpeg(w: int = 800, h: int = 600) -> bytes:
    out = io.BytesIO()
    Image.new("RGB", (w, h), (40, 140, 60)).save(out, "JPEG")
    return out.getvalue()


def meta(**changes) -> dict:
    m = {
        "schema_version": 1, "node_id": "cam-01", "ts": "2026-11-17T04:00:28Z",
        "timestamp": "2026-11-17T04:00:28Z", "camera_type": "xiao-ov3660",
        "trigger_type": "remote_command", "resolution": "SVGA", "width": 800, "height": 600,
        "priority": "high", "seq": 1,
        "sensor_snapshot": {"temperature": None, "humidity": None, "lux": None},
        "local_analysis": None,
    }
    m.update(changes)
    return m


async def upload(client: AsyncClient, *, token=TOKEN_CAM, m=None, img=None, key=None):
    headers = {"Authorization": f"Bearer {token}"} if token else {}
    if key:
        headers["Idempotency-Key"] = key
    files = {
        "image": ("x.jpg", img if img is not None else jpeg(), "image/jpeg"),
        "metadata": (None, json.dumps(m if m is not None else meta()), "application/json"),
    }
    return await client.post("/api/v1/captures", files=files, headers=headers)
