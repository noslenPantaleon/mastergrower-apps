"""Esquemas HTTP de nodos (contract/http/nodes.md)."""

from datetime import datetime
from typing import Any

from pydantic import BaseModel, ConfigDict, field_serializer

from app.schemas.common import iso_z


class NodeOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    node_id: str
    kind: str
    camera_type: str | None
    firmware: str | None
    zone_id: str | None
    node_state: str | None
    last_seen: datetime | None
    status_ts: datetime | None
    last_status: dict[str, Any] | None

    @field_serializer("last_seen", "status_ts")
    def _iso(self, dt: datetime | None) -> str | None:
        return iso_z(dt)


class NodeList(BaseModel):
    items: list[NodeOut]
