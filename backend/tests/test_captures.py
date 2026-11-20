import json
from io import BytesIO
from pathlib import Path
from typing import get_args

from PIL import Image

from tests.conftest import TOKEN_CAM2, TOKEN_USER, jpeg, meta, upload

USER = {"Authorization": f"Bearer {TOKEN_USER}"}


async def test_upload_and_read(client, settings):
    r = await upload(client)
    assert r.status_code == 201, r.text
    c = r.json()
    assert c["capture_id"] == "c-000001"
    assert c["timestamp"] == "2026-11-17T04:00:28Z"
    assert c["timestamp_approx"] is False
    assert c["priority"] == "high"
    assert Path(settings.images_dir, "cam-01/2026/11/17/c-000001.jpg").is_file()

    r = await client.get(f"/api/v1/captures/{c['capture_id']}", headers=USER)
    assert r.status_code == 200 and r.json()["resolution"] == "SVGA"

    r = await client.get(f"/api/v1/captures/{c['capture_id']}/image", headers=USER)
    assert r.status_code == 200 and r.headers["content-type"] == "image/jpeg"
    assert r.content.startswith(b"\xff\xd8")


async def test_thumb_is_generated_and_cached(client, settings):
    cid = (await upload(client)).json()["capture_id"]
    r = await client.get(f"/api/v1/captures/{cid}/thumb?token={TOKEN_USER}")
    assert r.status_code == 200
    assert Image.open(BytesIO(r.content)).size == (320, 240)
    assert Path(settings.images_dir, "cam-01/2026/11/17/c-000001_thumb.jpg").is_file()


async def test_idempotency(client):
    r1 = await upload(client, key="20261117T040028Z_h_715ade70")
    r2 = await upload(client, key="20261117T040028Z_h_715ade70")
    assert (r1.status_code, r2.status_code) == (201, 200)
    assert r1.json()["capture_id"] == r2.json()["capture_id"]
    # la misma clave en otro nodo es otra captura
    r3 = await upload(client, token=TOKEN_CAM2, m=meta(node_id="cam-02"),
                      key="20261117T040028Z_h_715ade70")
    assert r3.status_code == 201
    listing = (await client.get("/api/v1/captures", headers=USER)).json()
    assert len(listing["items"]) == 2


async def test_without_clock_uses_reception_time(client):
    r = await upload(client, m=meta(timestamp=None))
    assert r.status_code == 201
    assert r.json()["timestamp_approx"] is True


async def test_authentication(client):
    assert (await upload(client, token=None)).status_code == 401
    assert (await upload(client, token="fake")).status_code == 401
    r = await upload(client, token=TOKEN_USER)  # un usuario no sube
    assert r.status_code == 403
    r = await upload(client, m=meta(node_id="cam-02"))  # token de cam-01 con node_id ajeno
    assert r.status_code == 403
    body = r.json()
    assert set(body) == {"code", "message", "detail"} and body["code"] == "forbidden"
    assert (await client.get("/api/v1/captures")).status_code == 401


async def test_invalid_data(client):
    r = await upload(client, m=meta(trigger_type="other"))
    assert r.status_code == 422 and r.json()["detail"][0]["field"] == "trigger_type"
    assert (await upload(client, m=meta(schema_version=2))).status_code == 422
    assert (await upload(client, img=b"not a jpeg")).status_code == 415
    assert (await upload(client, img=b"\xff\xd8\xff" + b"0" * 300_000)).status_code == 413
    files = {"image": ("x.jpg", jpeg(), "image/jpeg"), "metadata": (None, "{broken")}
    r = await client.post("/api/v1/captures", files=files,
                          headers={"Authorization": "Bearer tok-cam-01"})
    assert r.status_code == 400


async def test_unknown_fields_are_ignored(client):
    r = await upload(client, m=meta(future_field=123))
    assert r.status_code == 201


async def test_listing_filters_and_cursor(client):
    for i in range(5):
        m = meta(timestamp=f"2026-11-17T0{i}:00:00Z", seq=i,
                 trigger_type="timer" if i % 2 else "remote_command",
                 priority="normal" if i % 2 else "high")
        assert (await upload(client, m=m)).status_code == 201

    p1 = (await client.get("/api/v1/captures?limit=2", headers=USER)).json()
    assert [c["seq"] for c in p1["items"]] == [4, 3]
    p2 = (await client.get(f"/api/v1/captures?limit=2&cursor={p1['next_cursor']}",
                           headers=USER)).json()
    assert [c["seq"] for c in p2["items"]] == [2, 1]
    p3 = (await client.get(f"/api/v1/captures?limit=2&cursor={p2['next_cursor']}",
                           headers=USER)).json()
    assert [c["seq"] for c in p3["items"]] == [0] and p3["next_cursor"] is None

    r = await client.get("/api/v1/captures?trigger_type=timer", headers=USER)
    assert [c["seq"] for c in r.json()["items"]] == [3, 1]
    r = await client.get("/api/v1/captures?since=2026-11-17T02:00:00Z&until=2026-11-17T04:00:00Z",
                         headers=USER)
    assert [c["seq"] for c in r.json()["items"]] == [3, 2]
    r = await client.get("/api/v1/captures?node_id=cam-02", headers=USER)
    assert r.json()["items"] == []


async def test_not_found(client):
    r = await client.get("/api/v1/captures/c-999999", headers=USER)
    assert r.status_code == 404 and r.json()["code"] == "not_found"
    assert (await client.get("/api/v1/captures/xyz", headers=USER)).status_code == 404


def test_contract_schema_matches():
    """Los enums del modelo Pydantic deben ser los del esquema en contract/."""
    from app.schemas.capture import CameraType, Priority, Resolution, TriggerType

    path = Path(__file__).parents[2] / "contract/http/schemas/capture_metadata.schema.json"
    props = json.loads(path.read_text())["properties"]
    for field, typ in [("camera_type", CameraType), ("trigger_type", TriggerType),
                       ("resolution", Resolution), ("priority", Priority)]:
        assert set(props[field]["enum"]) == set(get_args(typ)), field
