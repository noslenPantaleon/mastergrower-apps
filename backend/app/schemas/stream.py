"""Esquemas HTTP de la vista en vivo (contract/http/stream.md)."""

from datetime import datetime
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field, field_serializer

from app.schemas.common import iso_z

StreamResolution = Literal["qvga", "vga"]
StreamState = Literal["requested", "active", "ended", "error"]
RES_ORDER = {"qvga": 0, "vga": 1}


class StreamCreate(BaseModel):
    model_config = ConfigDict(extra="forbid")

    node_id: str
    resolution: StreamResolution = "qvga"
    fps_max: int | None = Field(None, ge=1, le=30)   # None = el máximo del nodo
    ttl_s: int | None = Field(None, ge=10, le=3600)  # None = 120 s (recortado al máximo)


class StreamOut(BaseModel):
    session_id: str
    node_id: str
    requested_by: str
    resolution: StreamResolution
    fps_max: int
    ttl_s: int
    stream_state: StreamState
    end_reason: str | None
    started_at: datetime
    expires_at: datetime
    ended_at: datetime | None
    publisher_connected: bool
    viewers: int
    fps: float
    frames: int
    bytes: int
    dropped: int

    @field_serializer("started_at", "expires_at", "ended_at")
    def _iso(self, dt: datetime | None) -> str | None:
        return iso_z(dt)


class StreamCreated(StreamOut):
    """Respuesta de POST: el token de espectador y las URLs de los WebSocket."""

    token: str
    ws_view_url: str
    ws_publish_url: str


class StreamList(BaseModel):
    items: list[StreamOut]
