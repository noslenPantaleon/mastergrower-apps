"""GET/PATCH /events y GET /nodes."""

from datetime import UTC, datetime, timedelta

from app.models.zone import Zone
from app.services.ingest import handle_message
from tests.conftest import TOKEN_CAM, USER, upload
from tests.test_ingest import event, status

NOW = datetime(2026, 11, 19, 21, 0, 0, tzinfo=UTC)
CAM = {"Authorization": f"Bearer {TOKEN_CAM}"}


async def _seed(db):
    db.add_all([Zone(zone_id="zone-a", name="A"), Zone(zone_id="zone-b", name="B")])
    await db.commit()
    await handle_message(db, "growlab/cam/cam-01/status", status(zone_id="zone-a"), now=NOW)
    await handle_message(db, "growlab/cam/cam-02/status",
                         status(node_id="cam-02", zone_id="zone-b"), now=NOW)
    for i, (node, sev, typ) in enumerate([
        ("cam-01", "medium", "sd_full"),
        ("cam-01", "high", "early_lights_off"),
        ("cam-02", "medium", "camera_error"),
    ]):
        ts = (NOW - timedelta(minutes=10 - i)).strftime("%Y-%m-%dT%H:%M:%SZ")
        await handle_message(db, f"growlab/cam/{node}/event",
                             event(node_id=node, severity=sev, event_type=typ, ts=ts), now=NOW)


async def test_list_events_filters_and_order(client, db):
    await _seed(db)
    r = await client.get("/api/v1/events", headers=USER)
    assert r.status_code == 200
    items = r.json()["items"]
    assert [e["event_type"] for e in items] == ["camera_error", "early_lights_off", "sd_full"]
    assert items[0]["event_id"].startswith("e-")
    assert items[0]["started_at"].endswith("Z")

    async def types(**params):
        r = await client.get("/api/v1/events", headers=USER, params=params)
        assert r.status_code == 200, r.text
        return [e["event_type"] for e in r.json()["items"]]

    assert await types(node_id="cam-01") == ["early_lights_off", "sd_full"]
    assert await types(severity="high") == ["early_lights_off"]
    assert await types(zone_id="zone-b") == ["camera_error"]
    assert await types(event_state="resolved") == []
    since = (NOW - timedelta(minutes=9)).strftime("%Y-%m-%dT%H:%M:%SZ")
    assert await types(since=since) == ["camera_error", "early_lights_off"]


async def test_list_events_pagination(client, db):
    await _seed(db)
    r1 = (await client.get("/api/v1/events", headers=USER, params={"limit": 2})).json()
    assert len(r1["items"]) == 2 and r1["next_cursor"]
    r2 = (await client.get("/api/v1/events", headers=USER,
                           params={"limit": 2, "cursor": r1["next_cursor"]})).json()
    assert [e["event_type"] for e in r2["items"]] == ["sd_full"]
    assert r2["next_cursor"] is None


async def test_patch_event_lifecycle(client, db):
    await _seed(db)
    eid = (await client.get("/api/v1/events", headers=USER)).json()["items"][0]["event_id"]
    url = f"/api/v1/events/{eid}"

    r = await client.patch(url, headers=USER, json={"event_state": "acknowledged"})
    assert r.status_code == 200
    body = r.json()
    assert body["event_state"] == "acknowledged"
    assert body["acknowledged_by"] == "dev" and body["acknowledged_at"]

    # idempotente
    assert (await client.patch(url, headers=USER, json={"event_state": "acknowledged"})
            ).status_code == 200
    r = await client.patch(url, headers=USER, json={"event_state": "resolved"})
    assert r.json()["event_state"] == "resolved" and r.json()["resolved_at"]

    r = await client.patch(url, headers=USER, json={"event_state": "acknowledged"})
    assert r.status_code == 409 and r.json()["code"] == "conflict"


async def test_patch_event_errors(client, db):
    await _seed(db)
    eid = (await client.get("/api/v1/events", headers=USER)).json()["items"][0]["event_id"]
    url = f"/api/v1/events/{eid}"
    assert (await client.patch(url, headers=CAM, json={"event_state": "resolved"})
            ).status_code == 403
    assert (await client.patch(url, json={"event_state": "resolved"})).status_code == 401
    r = await client.patch(url, headers=USER, json={"event_state": "open"})
    assert r.status_code == 422 and r.json()["code"] == "invalid_data"
    assert (await client.patch("/api/v1/events/e-999999", headers=USER,
                               json={"event_state": "resolved"})).status_code == 404
    assert (await client.get("/api/v1/events/zzz", headers=USER)).status_code == 404


async def test_nodes(client, db):
    await _seed(db)
    r = await client.get("/api/v1/nodes", headers=USER)
    assert r.status_code == 200
    items = r.json()["items"]
    assert [n["node_id"] for n in items] == ["cam-01", "cam-02"]
    assert items[0]["kind"] == "cam" and items[0]["last_seen"] == "2026-11-19T21:00:00Z"
    assert items[0]["last_status"]["sd_free_pct"] == 99

    r = await client.get("/api/v1/nodes", headers=USER, params={"zone_id": "zone-b"})
    assert [n["node_id"] for n in r.json()["items"]] == ["cam-02"]
    assert (await client.get("/api/v1/nodes/cam-02", headers=USER)).json()["zone_id"] == "zone-b"
    assert (await client.get("/api/v1/nodes/cam-99", headers=USER)).status_code == 404
    assert (await client.get("/api/v1/nodes")).status_code == 401


async def test_captures_zone_filter(client, db):
    await _seed(db)
    assert (await upload(client)).status_code == 201
    r = await client.get("/api/v1/captures", headers=USER, params={"zone_id": "zone-a"})
    assert len(r.json()["items"]) == 1
    r = await client.get("/api/v1/captures", headers=USER, params={"zone_id": "zone-b"})
    assert r.json()["items"] == []
