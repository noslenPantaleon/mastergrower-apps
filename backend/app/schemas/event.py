"""Esquemas HTTP de eventos (contract/http/events.md)."""

from datetime import datetime
from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, field_serializer

from app.schemas.common import iso_z


class EventOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    event_id: str
    node_id: str
    event_type: str
    severity: str
    event_state: str
    started_at: datetime
    acknowledged_at: datetime | None
    acknowledged_by: str | None
    resolved_at: datetime | None
    capture_id: str | None
    detail: Any
    received_at: datetime

    @field_serializer("started_at", "acknowledged_at", "resolved_at", "received_at")
    def _iso(self, dt: datetime | None) -> str | None:
        return iso_z(dt)


class EventList(BaseModel):
    items: list[EventOut]
    next_cursor: str | None


class EventPatch(BaseModel):
    model_config = ConfigDict(extra="forbid")

    event_state: Literal["acknowledged", "resolved"]
