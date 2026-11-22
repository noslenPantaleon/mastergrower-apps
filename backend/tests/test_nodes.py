"""Nodos (B4): alta con token, rotación, revocación, zona e importación de API_NODE_TOKENS."""

from app.services.nodes import import_env_tokens
from tests.conftest import TOKEN_CAM, USER, meta, upload

CAM1 = {"Authorization": f"Bearer {TOKEN_CAM}"}
N = "/api/v1/nodes"


def bearer(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


async def test_env_tokens_imported_once(client, maker):
    r = await client.get(f"{N}/cam-01", headers=USER)
    assert r.status_code == 200 and r.json()["registered_at"].endswith("Z")
    async with maker() as db:  # un segundo arranque no duplica
        assert await import_env_tokens(db, {"cam-01": TOKEN_CAM}) == 0


async def test_register_issues_working_token(client):
    r = await client.post(N, headers=USER, json={"node_id": "cam-03", "camera_type": "xiao-ov5640"})
    assert r.status_code == 201
    body = r.json()
    assert body["node_id"] == "cam-03" and len(body["token"]) >= 40
    assert body["node"]["kind"] == "cam" and body["node"]["camera_type"] == "xiao-ov5640"

    tok = bearer(body["token"])
    assert (await upload(client, token=body["token"], m=meta(node_id="cam-03"))).status_code == 201
    assert (await client.get("/api/v1/config/nodes/cam-03", headers=tok)).status_code == 200
    # el token de cam-03 no sirve para cam-01 ni para tareas de usuario
    assert (await client.get("/api/v1/config/nodes/cam-01", headers=tok)).status_code == 403
    assert (await client.post(N, headers=tok, json={"node_id": "cam-04"})).status_code == 403


async def test_register_twice_conflicts(client):
    assert (await client.post(N, headers=USER, json={"node_id": "cam-01"})).status_code == 409
    r = await client.post(N, headers=USER, json={"node_id": "ctrl-01", "camera_type": "rpi-noir"})
    assert r.status_code == 422
    assert (await client.post(N, headers=USER, json={"node_id": "cam-1"})).status_code == 422
    assert (await client.post(N, json={"node_id": "cam-05"})).status_code == 401


async def test_rotate_and_revoke(client):
    r = await client.post(f"{N}/cam-01/token", headers=USER)
    assert r.status_code == 201
    new = r.json()["token"]
    assert (await upload(client, token=TOKEN_CAM)).status_code == 401  # el viejo ya no sirve
    assert (await upload(client, token=new)).status_code == 201

    assert (await client.delete(f"{N}/cam-01/token", headers=USER)).status_code == 204
    assert (await upload(client, token=new, m=meta(seq=2))).status_code == 401
    assert (await client.post(f"{N}/cam-77/token", headers=USER)).status_code == 404


async def test_revoked_env_token_not_reimported(client, maker):
    assert (await client.delete(f"{N}/cam-01/token", headers=USER)).status_code == 204
    async with maker() as db:  # reinicio de la API con el mismo .env
        assert await import_env_tokens(db, {"cam-01": TOKEN_CAM}) == 0
    assert (await upload(client, token=TOKEN_CAM)).status_code == 401


async def test_assign_zone_bumps_config_version(client):
    cfg = "/api/v1/config/nodes/cam-01"
    v0 = (await client.get(cfg, headers=CAM1)).json()["version"]
    r = await client.put(f"{N}/cam-01/zone", headers=USER, json={"zone_id": "zone-x"})
    assert r.status_code == 422  # zona inexistente

    await client.post("/api/v1/zones", headers=USER, json={
        "zone_id": "zone-a", "name": "Carpa A", "light_cycle": {"start": "08:00", "end": "20:00"}})
    r = await client.put(f"{N}/cam-01/zone", headers=USER, json={"zone_id": "zone-a"})
    assert r.status_code == 200 and r.json()["zone_id"] == "zone-a"

    c = (await client.get(cfg, headers=CAM1)).json()
    assert c["version"] > v0
    assert c["config"]["light_window"] == {"start": "08:00", "end": "20:00"}
    assert c["sources"]["light_window"] == "zone"

    # repetir la misma zona no crea versión; quitarla vuelve a la global
    await client.put(f"{N}/cam-01/zone", headers=USER, json={"zone_id": "zone-a"})
    assert (await client.get(cfg, headers=CAM1)).json()["version"] == c["version"]
    r = await client.put(f"{N}/cam-01/zone", headers=USER, json={"zone_id": None})
    c2 = (await client.get(cfg, headers=CAM1)).json()
    assert c2["version"] > c["version"] and c2["sources"]["light_window"] == "default"

    items = (await client.get(N, headers=USER, params={"zone_id": "zone-a"})).json()["items"]
    assert items == []
    assert (await client.put(f"{N}/cam-55/zone", headers=USER,
                             json={"zone_id": None})).status_code == 404
