"""Zonas (B4): alta, cambios, borrado y la ventana de luz como nivel de configuración."""

from tests.conftest import TOKEN_CAM, USER

CAM1 = {"Authorization": f"Bearer {TOKEN_CAM}"}
Z = "/api/v1/zones"
CFG = "/api/v1/config/nodes/cam-01"


async def test_create_list_get(client):
    r = await client.post(Z, headers=USER, json={"zone_id": "zone-a", "name": "Carpa A",
                                                  "grow_mode": "dwc"})
    assert r.status_code == 201
    z = r.json()
    assert z["grow_mode"] == "dwc" and z["light_cycle"] is None and z["node_ids"] == []
    assert z["created_at"].endswith("Z")

    assert (await client.post(Z, headers=USER, json={"zone_id": "zone-a",
                                                      "name": "otra"})).status_code == 409
    assert (await client.post(Z, headers=USER, json={"zone_id": "Zona A",
                                                      "name": "x"})).status_code == 422
    assert (await client.post(Z, headers=USER, json={"zone_id": "zone-b", "name": "x",
                                                      "grow_mode": "aeroponia"})).status_code == 422
    assert (await client.post(Z, headers=CAM1, json={"zone_id": "zone-c",
                                                      "name": "x"})).status_code == 403

    items = (await client.get(Z, headers=USER)).json()["items"]
    assert [i["zone_id"] for i in items] == ["zone-a"]
    assert (await client.get(f"{Z}/zone-a", headers=CAM1)).status_code == 200
    assert (await client.get(f"{Z}/zone-q", headers=USER)).status_code == 404


async def test_light_cycle_resolution_and_versions(client):
    await client.post(Z, headers=USER, json={"zone_id": "zone-a", "name": "Carpa A"})
    await client.put("/api/v1/nodes/cam-01/zone", headers=USER, json={"zone_id": "zone-a"})
    await client.put("/api/v1/config/global", headers=USER,
                     json={"light_window": {"start": "07:00", "end": "23:00"}})
    c = (await client.get(CFG, headers=CAM1)).json()
    assert c["sources"]["light_window"] == "global"  # la zona todavía no fija ventana
    v = c["version"]

    r = await client.patch(f"{Z}/zone-a", headers=USER,
                           json={"light_cycle": {"start": "09:00", "end": "21:00"}})
    assert r.status_code == 200 and r.json()["node_ids"] == ["cam-01"]
    c = (await client.get(CFG, headers=CAM1)).json()
    assert c["version"] > v and c["config"]["light_window"]["start"] == "09:00"
    assert c["sources"]["light_window"] == "zone"
    v = c["version"]

    # cambiar solo el nombre no cambia la config del nodo
    r = await client.patch(f"{Z}/zone-a", headers=USER, json={"name": "Carpa grande"})
    assert r.json()["name"] == "Carpa grande"
    assert (await client.get(CFG, headers=CAM1)).json()["version"] == v

    # el override del nodo pisa a la zona
    await client.put(f"{CFG}/override", headers=USER,
                     json={"light_window": {"start": "10:00", "end": "22:00"}})
    c = (await client.get(CFG, headers=CAM1)).json()
    assert c["sources"]["light_window"] == "override"

    # quitar la ventana de la zona: nueva versión
    await client.delete(f"{CFG}/override", headers=USER)
    r = await client.patch(f"{Z}/zone-a", headers=USER, json={"light_cycle": None})
    c = (await client.get(CFG, headers=CAM1)).json()
    assert c["sources"]["light_window"] == "global" and c["version"] > v

    assert (await client.patch(f"{Z}/zone-a", headers=USER,
                               json={"name": None})).status_code == 422


async def test_delete_requires_empty_zone(client):
    await client.post(Z, headers=USER, json={"zone_id": "zone-a", "name": "A"})
    await client.put("/api/v1/nodes/cam-01/zone", headers=USER, json={"zone_id": "zone-a"})
    r = await client.delete(f"{Z}/zone-a", headers=USER)
    assert r.status_code == 409 and r.json()["detail"] == {"node_ids": ["cam-01"]}

    await client.put("/api/v1/nodes/cam-01/zone", headers=USER, json={"zone_id": None})
    assert (await client.delete(f"{Z}/zone-a", headers=USER)).status_code == 204
    assert (await client.get(f"{Z}/zone-a", headers=USER)).status_code == 404


async def test_filters_by_zone(client):
    await client.post(Z, headers=USER, json={"zone_id": "zone-a", "name": "A"})
    await client.put("/api/v1/nodes/cam-01/zone", headers=USER, json={"zone_id": "zone-a"})
    items = (await client.get("/api/v1/nodes", headers=USER,
                              params={"zone_id": "zone-a"})).json()["items"]
    assert [i["node_id"] for i in items] == ["cam-01"]
