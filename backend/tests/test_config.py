"""Configuración: override → global → default, versión, ETag y permisos."""

import json
from pathlib import Path

from tests.conftest import TOKEN_CAM, USER

CAM1 = {"Authorization": f"Bearer {TOKEN_CAM}"}
G = "/api/v1/config/global"
N = "/api/v1/config/nodes/cam-01"


async def test_defaults_without_changes(client):
    body = (await client.get(G, headers=USER)).json()
    assert body["version"] == 0 and body["updated_at"] is None
    assert body["config"]["capture_interval_min"] == 120
    assert body["config"]["light_window"] == {"start": "06:00", "end": "00:00"}
    assert set(body["sources"].values()) == {"default"}

    node = (await client.get(N, headers=CAM1)).json()
    assert node["node_id"] == "cam-01" and node["version"] == 0
    assert node["config"] == body["config"]


async def test_resolution_order_and_versions(client):
    r = await client.put(G, headers=USER, json={"capture_interval_min": 60, "jpeg_quality": 10})
    assert r.status_code == 200
    g = r.json()
    assert g["version"] == 1 and g["updated_at"].endswith("Z")
    assert g["sources"]["capture_interval_min"] == "global"

    r = await client.put(f"{N}/override", headers=USER,
                         json={"capture_interval_min": 15, "light_window": {"start": "08:00",
                                                                            "end": "20:00"}})
    assert r.status_code == 200
    n = r.json()
    assert n["version"] == 2
    assert n["config"]["capture_interval_min"] == 15
    assert n["config"]["jpeg_quality"] == 10
    assert n["config"]["resolution"] == "SVGA"
    assert n["sources"] == {**n["sources"], "capture_interval_min": "override",
                            "light_window": "override", "jpeg_quality": "global",
                            "resolution": "default"}

    # Otro nodo no ve el override de cam-01, y su versión no incluye la revisión 2
    other = (await client.get("/api/v1/config/nodes/cam-02", headers=USER)).json()
    assert other["config"]["capture_interval_min"] == 60 and other["version"] == 1
    # La global tampoco cambió de versión por un override
    assert (await client.get(G, headers=USER)).json()["version"] == 1

    # null borra: el override vuelve al global
    n = (await client.put(f"{N}/override", headers=USER,
                          json={"capture_interval_min": None})).json()
    assert n["config"]["capture_interval_min"] == 60 and n["version"] == 3
    ov = (await client.get(f"{N}/override", headers=USER)).json()
    assert ov["override"] == {"light_window": {"start": "08:00", "end": "20:00"}}

    # Un cambio global sube la versión de todos los nodos
    await client.put(G, headers=USER, json={"capture_interval_min": None})
    n = (await client.get(N, headers=USER)).json()
    assert n["version"] == 4 and n["config"]["capture_interval_min"] == 120


async def test_no_change_no_revision(client):
    await client.put(G, headers=USER, json={"jpeg_quality": 10})
    await client.put(G, headers=USER, json={"jpeg_quality": 10})
    await client.put(G, headers=USER, json={"resolution": None})  # no existía
    assert (await client.get(G, headers=USER)).json()["version"] == 1


async def test_delete_override(client):
    await client.put(f"{N}/override", headers=USER, json={"analysis_mode": "hsv"})
    assert (await client.delete(f"{N}/override", headers=USER)).status_code == 204
    n = (await client.get(N, headers=USER)).json()
    assert n["config"]["analysis_mode"] == "hybrid" and n["version"] == 2
    assert (await client.delete(f"{N}/override", headers=USER)).status_code == 204
    assert (await client.get(N, headers=USER)).json()["version"] == 2


async def test_etag_304(client):
    r = await client.get(N, headers=CAM1)
    assert r.headers["etag"] == '"0"'
    r = await client.get(N, headers={**CAM1, "If-None-Match": '"0"'})
    assert r.status_code == 304 and r.content == b""
    await client.put(G, headers=USER, json={"stream_fps_max": 5})
    r = await client.get(N, headers={**CAM1, "If-None-Match": '"0"'})
    assert r.status_code == 200 and r.headers["etag"] == '"1"'


async def test_validation_errors(client):
    r = await client.put(G, headers=USER, json={
        "capture_interval_min": 0, "nope": 1, "light_window": {"start": "25:00", "end": "10:00"},
        "analysis_mode": "magic",
    })
    assert r.status_code == 422 and r.json()["code"] == "invalid_data"
    fields = {d["field"] for d in r.json()["detail"]}
    assert {"capture_interval_min", "nope", "light_window.start", "analysis_mode"} <= fields
    assert (await client.get(G, headers=USER)).json()["version"] == 0  # nada aplicado

    assert (await client.put(G, headers=USER, json=[1, 2])).status_code == 422
    r = await client.put("/api/v1/config/nodes/robot-7/override", headers=USER,
                         json={"jpeg_quality": 10})
    assert r.status_code == 404


async def test_permissions(client):
    assert (await client.get(G)).status_code == 401
    assert (await client.get(G, headers=CAM1)).status_code == 200
    assert (await client.put(G, headers=CAM1, json={"jpeg_quality": 10})).status_code == 403
    assert (await client.put(f"{N}/override", headers=CAM1,
                             json={"jpeg_quality": 10})).status_code == 403
    assert (await client.delete(f"{N}/override", headers=CAM1)).status_code == 403
    # Un nodo solo lee su propia configuración
    assert (await client.get("/api/v1/config/nodes/cam-02", headers=CAM1)).status_code == 403


async def test_non_camera_nodes_have_empty_config(client):
    r = await client.get("/api/v1/config/nodes/ctrl-01", headers=USER)
    assert r.status_code == 200 and r.json()["config"] == {}
    r = await client.put("/api/v1/config/nodes/ctrl-01/override", headers=USER,
                         json={"jpeg_quality": 10})
    assert r.status_code == 422


def test_contract_schema_matches_params():
    from app.schemas.config import CamParams, defaults

    path = Path(__file__).parents[2] / "contract/http/schemas/cam_config.schema.json"
    props = json.loads(path.read_text())["properties"]
    assert set(props) == set(CamParams.model_fields)
    for key, value in defaults(CamParams).items():
        assert props[key]["default"] == value, key


def test_defaults_match_firmware_kconfig():
    """Los defaults de la API deben ser los del firmware (último nivel en la cámara)."""
    import re

    from app.schemas.config import CamParams, defaults

    kconfig = (Path(__file__).parents[2] / "camera_node/main/Kconfig.projbuild").read_text()

    def kdefault(name: str) -> str:
        m = re.search(rf"config {name}\n(?:.*\n)*?\s+default (.+)", kconfig)
        assert m, name
        return m.group(1).strip().strip('"')

    d = defaults(CamParams)
    assert int(kdefault("MG_CAPTURE_INTERVAL_MIN")) == d["capture_interval_min"]
    assert kdefault("MG_LIGHT_WINDOW_START") == d["light_window"]["start"]
    assert kdefault("MG_LIGHT_WINDOW_END") == d["light_window"]["end"]
    assert int(kdefault("MG_UTC_OFFSET_MIN")) == d["utc_offset_min"]
    assert int(kdefault("MG_TRIGGER_MIN_INTERVAL_S")) == d["min_trigger_interval_s"]
    assert int(kdefault("MG_CAPTURE_JPEG_QUALITY")) == d["jpeg_quality"]
    assert kdefault("MG_CAPTURE_RES") == d["resolution"]
    assert kdefault("MG_CAPTURE_RES_FULL") == d["resolution_full"]
