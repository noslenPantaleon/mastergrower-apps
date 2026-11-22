"""Esquemas HTTP de nodos (contract/http/nodes.md)."""

from datetime import datetime
from typing import Any

from pydantic import BaseModel, ConfigDict, Field, field_serializer

from app.schemas.common import CameraType, iso_z

NODE_ID = r"^(cam|ctrl|disp)-[0-9]{2,}$"


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
    registered_at: datetime | None
    last_status: dict[str, Any] | None

    @field_serializer("last_seen", "status_ts", "registered_at")
    def _iso(self, dt: datetime | None) -> str | None:
        return iso_z(dt)


class NodeList(BaseModel):
    items: list[NodeOut]


class NodeCreate(BaseModel):
    model_config = ConfigDict(extra="forbid")

    node_id: str = Field(pattern=NODE_ID, max_length=32, examples=["cam-02"])
    camera_type: CameraType | None = None
    zone_id: str | None = None


class NodeZoneIn(BaseModel):
    model_config = ConfigDict(extra="forbid")

    zone_id: str | None  # null = sin zona


class NodeTokenOut(BaseModel):
    """El token en claro: se entrega una sola vez (la API guarda solo su hash)."""

    node_id: str
    token: str


class NodeRegistered(NodeTokenOut):
    node: NodeOut
