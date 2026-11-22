"""Ingestor MQTT sin broker: se le pasan tema y payload directamente."""

import json
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import get_args

from sqlalchemy import select

from app.models import Event, Node, Zone
from app.services.ingest import handle_message, parse_topic

NOW = datetime(2026, 11, 19, 21, 0, 0, tzinfo=UTC)


def status(**changes) -> bytes:
    m = {
        "schema_version": 1, "node_id": "cam-01", "ts": "2026-11-19T20:59:58Z",
        "node_state": "operational", "camera_type": "xiao-ov3660", "firmware": "0.1.0",
        "last_capture": None, "sd_free_pct": 99, "rssi": -60, "zone_id": None,
    }
    m.update(changes)
    return json.dumps(m).encode()


def event(**changes) -> bytes:
    m = {"schema_version": 1, "node_id": "cam-01", "ts": "2026-11-19T20:30:00Z",
         "event_type": "sd_full", "severity": "medium", "capture_id": None,
         "detail": "SD al 81 %"}
    m.update(changes)
    return json.dumps(m).encode()


async def _events(db, **where) -> list[Event]:
    q = select(Event).filter_by(**where).order_by(Event.id)
    return list((await db.execute(q)).scalars())


def test_parse_topic():
    assert parse_topic("growlab/cam/cam-01/status").channel == "status"
    assert parse_topic("growlab/ctrl/ctrl-01/state").kind == "ctrl"
    assert parse_topic("growlab/cam/cam-01/cmd") is None
    assert parse_topic("growlab/cam/cam-01/cmd/ack") is None
    assert parse_topic("otro/cam/cam-01/status") is None


async def test_status_creates_and_updates_node(db):
    assert await handle_message(db, "growlab/cam/cam-01/status", status(), now=NOW) == "status"
    n = await db.get(Node, "cam-01")
    assert (n.kind, n.node_state, n.firmware, n.camera_type) == (
        "cam", "operational", "0.1.0", "xiao-ov3660")
    assert n.last_seen.replace(tzinfo=UTC) == NOW
    assert n.last_status["rssi"] == -60

    later = NOW + timedelta(minutes=5)
    await handle_message(db, "growlab/cam/cam-01/status",
                         status(firmware="0.2.0", rssi=-70), now=later)
    await db.refresh(n)
    assert n.firmware == "0.2.0" and n.last_status["rssi"] == -70


async def test_retained_status_uses_message_ts(db):
    await handle_message(db, "growlab/cam/cam-01/status", status(), retained=True, now=NOW)
    n = await db.get(Node, "cam-01")
    assert n.last_seen.replace(tzinfo=UTC) == datetime(2026, 11, 19, 20, 59, 58, tzinfo=UTC)


async def test_zone_from_node_only_if_unset_and_known(db):
    await handle_message(db, "growlab/cam/cam-01/status", status(zone_id="zone-q"), now=NOW)
    assert (await db.get(Node, "cam-01")).zone_id is None  # zona inexistente: se ignora
    db.add_all([Zone(zone_id="zone-a", name="A"), Zone(zone_id="zone-b", name="B")])
    await db.commit()
    await handle_message(db, "growlab/cam/cam-01/status", status(zone_id="zone-a"), now=NOW)
    await handle_message(db, "growlab/cam/cam-01/status", status(zone_id="zone-b"), now=NOW)
    assert (await db.get(Node, "cam-01")).zone_id == "zone-a"


async def test_lwt_opens_and_reconnect_resolves(db):
    topic = "growlab/cam/cam-01/status"
    await handle_message(db, topic, status(), now=NOW)
    # LWT con ts viejo (de cuando se conectó) y repetido: un solo evento, con la hora de recepción
    lwt = status(node_state="offline", ts="1970-01-01T00:00:05Z")
    await handle_message(db, topic, lwt, now=NOW + timedelta(minutes=1))
    await handle_message(db, topic, lwt, now=NOW + timedelta(minutes=2))
    evs = await _events(db, event_type="node_disconnected")
    assert len(evs) == 1
    assert evs[0].event_state == "open"
    assert evs[0].started_at.replace(tzinfo=UTC) == NOW + timedelta(minutes=1)
    assert (await db.get(Node, "cam-01")).node_state == "offline"

    await handle_message(db, topic, status(node_state="booting"), now=NOW + timedelta(minutes=3))
    await db.refresh(evs[0])
    assert evs[0].event_state == "resolved"
    assert evs[0].resolved_at.replace(tzinfo=UTC) == NOW + timedelta(minutes=3)

    # Una nueva caída abre otro evento
    await handle_message(db, topic, lwt, now=NOW + timedelta(minutes=10))
    assert len(await _events(db, event_type="node_disconnected", event_state="open")) == 1


async def test_event_stored_and_deduplicated(db):
    topic = "growlab/cam/cam-01/event"
    assert await handle_message(db, topic, event(), now=NOW) == "event"
    assert await handle_message(db, topic, event(), now=NOW) == "duplicate"
    evs = await _events(db)
    assert len(evs) == 1
    e = evs[0]
    assert (e.event_type, e.severity, e.event_state, e.detail) == (
        "sd_full", "medium", "open", "SD al 81 %")
    assert e.started_at.replace(tzinfo=UTC) == datetime(2026, 11, 19, 20, 30, tzinfo=UTC)


async def test_event_without_ntp_uses_reception_time(db):
    await handle_message(db, "growlab/cam/cam-01/event",
                         event(ts="1970-01-01T00:00:12Z", detail={"x": 1}), now=NOW)
    e = (await _events(db))[0]
    assert e.started_at.replace(tzinfo=UTC) == NOW
    assert e.detail == {"x": 1}


async def test_invalid_messages_are_dropped(db):
    t = "growlab/cam/cam-01/event"
    assert await handle_message(db, t, b"no json", now=NOW) == "invalid"
    assert await handle_message(db, t, b"[1,2]", now=NOW) == "invalid"
    assert await handle_message(db, t, event(node_id="cam-02"), now=NOW) == "invalid"
    assert await handle_message(db, t, event(severity="critica"), now=NOW) == "invalid"
    assert await handle_message(db, t, event(schema_version=2), now=NOW) == "invalid"
    assert await handle_message(db, "growlab/cam/cam-01/status",
                                status(node_state="operativo"), now=NOW) == "invalid"
    assert await _events(db) == []
    assert await db.get(Node, "cam-01") is None


def test_mqtt_contract_enums_match():
    from app.schemas.common import EventType, NodeState, Severity

    base = Path(__file__).parents[2] / "contract/mqtt/schemas"
    st = json.loads((base / "cam_status.schema.json").read_text())["properties"]
    ev = json.loads((base / "cam_event.schema.json").read_text())["properties"]
    assert set(st["node_state"]["enum"]) == set(get_args(NodeState))
    assert set(ev["event_type"]["enum"]) == set(get_args(EventType))
    assert set(ev["severity"]["enum"]) == set(get_args(Severity))
