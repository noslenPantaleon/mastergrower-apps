"""Consulta y cambio de estado de eventos (contract/http/events.md)."""

from datetime import UTC, datetime

from sqlalchemy import and_, or_, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.errors import ApiError
from app.core.pagination import decode_cursor, encode_cursor
from app.models.event import Event, parse_event_id
from app.models.node import Node


async def list_events(
    db: AsyncSession,
    *,
    node_id: str | None = None,
    zone_id: str | None = None,
    severity: str | None = None,
    event_state: str | None = None,
    event_type: str | None = None,
    since: datetime | None = None,
    until: datetime | None = None,
    limit: int = 50,
    cursor: str | None = None,
) -> tuple[list[Event], str | None]:
    q = select(Event)
    if node_id:
        q = q.where(Event.node_id == node_id)
    if zone_id:
        q = q.where(Event.node_id.in_(select(Node.node_id).where(Node.zone_id == zone_id)))
    if severity:
        q = q.where(Event.severity == severity)
    if event_state:
        q = q.where(Event.event_state == event_state)
    if event_type:
        q = q.where(Event.event_type == event_type)
    if since:
        q = q.where(Event.started_at >= since)
    if until:
        q = q.where(Event.started_at < until)
    if cursor:
        cts, cid = decode_cursor(cursor)
        q = q.where(or_(Event.started_at < cts, and_(Event.started_at == cts, Event.id < cid)))
    q = q.order_by(Event.started_at.desc(), Event.id.desc()).limit(limit + 1)
    rows = list((await db.execute(q)).scalars())
    last = rows[limit - 1] if len(rows) > limit else None
    nxt = encode_cursor(last.started_at, last.id) if last else None
    return rows[:limit], nxt


async def get_event(db: AsyncSession, event_id: str) -> Event:
    n = parse_event_id(event_id)
    ev = await db.get(Event, n) if n is not None else None
    if ev is None:
        raise ApiError(404, f"No existe el evento {event_id}")
    return ev


async def change_state(db: AsyncSession, ev: Event, new_state: str, user: str) -> Event:
    if ev.event_state == new_state:
        return ev  # idempotente
    if ev.event_state == "resolved":
        raise ApiError(409, "El evento ya está resuelto", code="conflict",
                       detail={"event_state": ev.event_state})
    now = datetime.now(UTC)
    if new_state == "acknowledged":
        ev.acknowledged_at, ev.acknowledged_by = now, user
    else:  # resolved (desde open o acknowledged)
        ev.resolved_at = now
    ev.event_state = new_state
    await db.commit()
    return ev
