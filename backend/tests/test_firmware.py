"""Firmware y OTA: lectura de esp_app_desc_t, subida, descarga y ota_update."""

import hashlib
import struct

from app.models.node import Node
from tests.conftest import TOKEN_CAM, USER

F = "/api/v1/firmware"


def app_bin(project: str = "camera_node", version: str = "0.2.0", size: int = 4096) -> bytes:
    """Imagen mínima con la estructura de ESP-IDF: cabecera 0xE9, segmento y esp_app_desc_t."""
    def fixed(s: str, n: int) -> bytes:
        return s.encode().ljust(n, b"\0")

    desc = (struct.pack("<II", 0xABCD5432, 0) + b"\0" * 8 + fixed(version, 32) + fixed(project, 32)
            + fixed("12:00:00", 16) + fixed("Nov 22 2026", 16) + fixed("v6.1", 32))
    data = bytes([0xE9]) + b"\0" * 31 + desc
    return data.ljust(size, b"\xff")


def files(data: bytes) -> dict:
    return {"file": ("fw.bin", data, "application/octet-stream")}


async def test_upload_reads_version_and_kind(client):
    data = app_bin()
    r = await client.post(F, headers=USER, files=files(data))
    assert r.status_code == 201, r.text
    fw = r.json()
    assert (fw["kind"], fw["version"], fw["project"]) == ("cam", "0.2.0", "camera_node")
    assert fw["sha256"] == hashlib.sha256(data).hexdigest() and fw["size"] == len(data)
    assert fw["download_url"] == f"/api/v1/firmware/{fw['firmware_id']}/download"

    # misma versión otra vez: conflicto; otro proyecto u otra cosa: rechazo
    assert (await client.post(F, headers=USER, files=files(data))).status_code == 409
    assert (await client.post(F, headers=USER, files=files(app_bin("otro")))).status_code == 422
    assert (await client.post(F, headers=USER, files=files(b"hola" * 100))).status_code == 422
    cam = {"Authorization": f"Bearer {TOKEN_CAM}"}
    assert (await client.post(F, headers=cam, files=files(app_bin(version="9")))).status_code == 403


async def test_download_list_latest(client):
    a = (await client.post(F, headers=USER, files=files(app_bin(version="0.2.0")))).json()
    b = (await client.post(F, headers=USER, files=files(app_bin(version="0.3.0")))).json()
    await client.post(F, headers=USER, files=files(app_bin("display_node", "0.2.0")))

    cam = {"Authorization": f"Bearer {TOKEN_CAM}"}
    r = await client.get(a["download_url"], headers=cam)  # el nodo baja con su token
    assert r.status_code == 200 and r.content == app_bin(version="0.2.0")
    assert r.headers["x-firmware-sha256"] == a["sha256"]

    latest = (await client.get(f"{F}/cam/latest", headers=USER)).json()
    assert latest["firmware_id"] == b["firmware_id"]
    items = (await client.get(F, headers=USER, params={"kind": "disp"})).json()["items"]
    assert [i["project"] for i in items] == ["display_node"]
    assert (await client.get(f"{F}/ctrl/latest", headers=USER)).status_code == 404

    assert (await client.delete(f"{F}/{a['firmware_id']}", headers=USER)).status_code == 204
    assert (await client.get(a["download_url"], headers=USER)).status_code == 404


async def test_deploy_sends_ota_update(client, app, db):
    db.add_all([Node(node_id="cam-07", kind="cam", node_state="operational"),
                Node(node_id="cam-08", kind="cam", node_state="offline"),
                Node(node_id="disp-01", kind="disp", node_state="operational")])
    await db.commit()
    fw = (await client.post(F, headers=USER, files=files(app_bin()))).json()
    r = await client.post(f"{F}/{fw['firmware_id']}/deploy", headers=USER,
                          json={"node_ids": ["cam-07", "cam-08", "disp-01", "cam-99"]})
    assert r.status_code == 200
    res = {x["node_id"]: x for x in r.json()["results"]}
    assert res["cam-07"]["sent"] and res["cam-07"]["cmd_id"].startswith("ota-")
    assert res["cam-08"]["error"] == "node_offline"
    assert res["disp-01"]["error"] == "wrong_kind"
    assert res["cam-99"]["error"] == "not_found"

    cmd = app.state.hub.send_cmd[-1]
    assert cmd["action"] == "ota_update" and cmd["node_id"] == "cam-07"
    assert cmd["params"] == {"url": fw["download_url"], "version": "0.2.0", "sha256": fw["sha256"],
                             "size": fw["size"]}
