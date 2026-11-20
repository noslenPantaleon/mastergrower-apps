"""Eventos (Contrato §5.3, contract/http/events.md)."""

from datetime import datetime
from typing import Annotated

from fastapi import APIRouter, Depends, Query
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.db import get_session
from app.core.security import ReaderDep, UserDep
from app.models.event import Event
from app.schemas.common import EventState, EventType, Severity
from app.schemas.event import EventList, EventOut, EventPatch
from app.services import events as svc

router = APIRouter(prefix="/events", tags=["events"])

DbDep = Annotated[AsyncSession, Depends(get_session)]


@router.get("", response_model=EventList)
async def list_events(
    _: ReaderDep,
    db: DbDep,
    node_id: str | None = None,
    zone_id: str | None = None,
    severity: Severity | None = None,
    event_state: EventState | None = None,
    event_type: EventType | None = None,
    since: datetime | None = None,
    until: datetime | None = None,
    limit: Annotated[int, Query(ge=1, le=200)] = 50,
    cursor: str | None = None,
) -> EventList:
    rows, nxt = await svc.list_events(
        db, node_id=node_id, zone_id=zone_id, severity=severity, event_state=event_state,
        event_type=event_type, since=since, until=until, limit=limit, cursor=cursor,
    )
    return EventList(items=[EventOut.model_validate(r) for r in rows], next_cursor=nxt)


@router.get("/{event_id}", response_model=EventOut)
async def get_event(event_id: str, _: ReaderDep, db: DbDep) -> Event:
    return await svc.get_event(db, event_id)


@router.patch("/{event_id}", response_model=EventOut)
async def patch_event(event_id: str, body: EventPatch, user: UserDep, db: DbDep) -> Event:
    ev = await svc.get_event(db, event_id)
    return await svc.change_state(db, ev, body.event_state, user.name)
