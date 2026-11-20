"""Mensajes MQTT que consume la API (contract/mqtt/schemas/). Campos desconocidos se ignoran."""

from datetime import datetime
from typing import Any, Literal

from pydantic import BaseModel, ConfigDict

from app.schemas.common import EventType, NodeState, Severity


class StatusMsg(BaseModel):
    """growlab/{kind}/{id}/status y growlab/ctrl/{id}/state (campos mínimos comunes)."""

    model_config = ConfigDict(extra="allow")

    schema_version: Literal[1]
    node_id: str
    ts: datetime
    node_state: NodeState
    camera_type: str | None = None
    firmware: str | None = None
    zone_id: str | None = None


class EventMsg(BaseModel):
    """growlab/{kind}/{id}/event (cam_event.schema.json)."""

    model_config = ConfigDict(extra="ignore")

    schema_version: Literal[1]
    node_id: str
    ts: datetime
    event_type: EventType
    severity: Severity
    capture_id: str | None = None
    detail: dict[str, Any] | str | None = None
