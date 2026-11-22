"""Procesa los mensajes MQTT de los nodos: status/state → tabla nodes, event → tabla events.

No conoce aiomqtt: recibe tema, payload y si era retenido, así se prueba sin broker.
"""

import json
import logging
from dataclasses import dataclass
from datetime import UTC, datetime

from pydantic import ValidationError
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models.event import Event
from app.models.node import Node
from app.models.zone import Zone
from app.schemas.mqtt import EventMsg, StatusMsg

log = logging.getLogger(__name__)

KINDS = {"cam", "ctrl", "disp"}
STATUS_CHANNELS = {"status", "state"}
# Antes de esto el nodo no tenía hora (NTP sin sincronizar: sale 1970)
MIN_VALID_TS = datetime(2020, 1, 1, tzinfo=UTC)


@dataclass(frozen=True)
class Topic:
    kind: str
    node_id: str
    channel: str


def parse_topic(topic: str) -> Topic | None:
    """growlab/{kind}/{id}/{channel} → Topic (None si no es un tema que la API consume)."""
    parts = topic.split("/")
    if len(parts) != 4 or parts[0] != "growlab" or parts[1] not in KINDS:
        return None
    if parts[3] not in STATUS_CHANNELS | {"event"}:
        return None
    return Topic(parts[1], parts[2], parts[3])


def _valid_ts(ts: datetime, fallback: datetime) -> datetime:
    ts = ts if ts.tzinfo else ts.replace(tzinfo=UTC)
    return ts if ts >= MIN_VALID_TS else fallback


async def handle_message(
    db: AsyncSession, topic: str, payload: bytes, *, retained: bool = False,
    now: datetime | None = None,
) -> str:
    """Procesa un mensaje y hace commit. Devuelve qué pasó (para log y tests)."""
    now = now or datetime.now(UTC)
    t = parse_topic(topic)
    if t is None:
        return "ignored"
    try:
        data = json.loads(payload)
        if not isinstance(data, dict):
            raise ValueError("el payload no es un objeto JSON")
    except ValueError as e:
        log.warning("MQTT %s: JSON inválido (%s)", topic, e)
        return "invalid"
    if data.get("node_id") != t.node_id:
        log.warning("MQTT %s: node_id %r no coincide con el tema", topic, data.get("node_id"))
        return "invalid"

    try:
        if t.channel in STATUS_CHANNELS:
            result = await _status(db, t, StatusMsg.model_validate(data), data, retained, now)
        else:
            result = await _event(db, EventMsg.model_validate(data), now)
    except ValidationError as e:
        errs = "; ".join(f"{'.'.join(map(str, x['loc']))}: {x['msg']}" for x in e.errors())
        log.warning("MQTT %s: no cumple el contrato (%s)", topic, errs)
        return "invalid"
    await db.commit()
    return result


async def _status(
    db: AsyncSession, t: Topic, msg: StatusMsg, raw: dict, retained: bool, now: datetime
) -> str:
    node = await db.get(Node, t.node_id)
    if node is None:
        node = Node(node_id=t.node_id, kind=t.kind)
        db.add(node)
        log.info("Nodo nuevo: %s (%s)", t.node_id, t.kind)

    ts = _valid_ts(msg.ts, now)
    prev_state = node.node_state
    node.kind = t.kind
    node.node_state = msg.node_state
    node.status_ts = ts
    node.last_status = raw
    if msg.camera_type:
        node.camera_type = msg.camera_type
    if msg.firmware:
        node.firmware = msg.firmware
    # La zona la asigna la API (PUT /nodes/{id}/zone); la que informa el nodo solo se adopta si
    # el nodo no tiene una y esa zona existe
    if msg.zone_id and not node.zone_id and await db.get(Zone, msg.zone_id) is not None:
        node.zone_id = msg.zone_id

    if msg.node_state == "offline":
        # El LWT trae el ts del momento en que el nodo se conectó: no sirve como hora de caída
        started = ts if retained else now
        await _open_disconnected(db, t.node_id, started)
    else:
        # Un retenido (la API se reinició) no prueba que el nodo esté vivo ahora
        node.last_seen = ts if retained else now
        await _resolve_disconnected(db, t.node_id, now)

    if prev_state != msg.node_state:
        log.info("Nodo %s: %s → %s", t.node_id, prev_state or "-", msg.node_state)
    return "status"


async def _open_events(db: AsyncSession, node_id: str, event_type: str) -> list[Event]:
    q = select(Event).where(
        Event.node_id == node_id, Event.event_type == event_type, Event.event_state != "resolved"
    )
    return list((await db.execute(q)).scalars())


async def _open_disconnected(db: AsyncSession, node_id: str, started: datetime) -> None:
    if await _open_events(db, node_id, "node_disconnected"):
        return
    db.add(Event(
        node_id=node_id, event_type="node_disconnected", severity="medium", event_state="open",
        started_at=started, detail="El broker publicó el LWT: el nodo se desconectó sin avisar",
    ))
    log.warning("Nodo %s fuera de línea: evento node_disconnected abierto", node_id)


async def _resolve_disconnected(db: AsyncSession, node_id: str, now: datetime) -> None:
    for ev in await _open_events(db, node_id, "node_disconnected"):
        ev.event_state = "resolved"
        ev.resolved_at = now
        log.info("Nodo %s de vuelta: node_disconnected resuelto", node_id)


async def _event(db: AsyncSession, msg: EventMsg, now: datetime) -> str:
    started = _valid_ts(msg.ts, now)
    dup = select(Event.id).where(
        Event.node_id == msg.node_id, Event.event_type == msg.event_type,
        Event.started_at == started,
    )
    if (await db.execute(dup)).first():
        return "duplicate"
    db.add(Event(
        node_id=msg.node_id, event_type=msg.event_type, severity=msg.severity,
        event_state="open", started_at=started, capture_id=msg.capture_id, detail=msg.detail,
    ))
    log.info("Evento %s de %s (%s)", msg.event_type, msg.node_id, msg.severity)
    return "event"
