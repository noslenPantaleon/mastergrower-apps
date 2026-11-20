"""Esquemas de capturas (contract/http/schemas/capture_metadata.schema.json)."""

from datetime import datetime
from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field, field_serializer

from app.schemas.common import CameraType, iso_z

TriggerType = Literal["timer", "sensor_event", "remote_command", "post_actuation", "schedule"]
Resolution = Literal["QVGA", "VGA", "SVGA", "XGA", "HD", "SXGA", "UXGA", "QXGA"]
Priority = Literal["normal", "high"]


class SensorSnapshot(BaseModel):
    model_config = ConfigDict(extra="allow")
    temperature: float | None = None
    humidity: float | None = None
    lux: float | None = None


class CaptureMetadata(BaseModel):
    """Parte 'metadata' del POST. Campos desconocidos se ignoran (Contrato §8.1)."""

    model_config = ConfigDict(extra="ignore")

    schema_version: Literal[1]
    node_id: str = Field(pattern=r"^cam-[0-9]{2,}$")
    ts: datetime | None = None
    timestamp: datetime | None = None
    camera_type: CameraType
    trigger_type: TriggerType
    resolution: Resolution
    width: int | None = Field(default=None, ge=1)
    height: int | None = Field(default=None, ge=1)
    priority: Priority
    seq: int | None = Field(default=None, ge=0)
    sensor_snapshot: SensorSnapshot | None = None
    local_analysis: dict[str, Any] | None = None


class CaptureOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    capture_id: str
    node_id: str
    timestamp: datetime
    timestamp_approx: bool
    received_at: datetime
    camera_type: str
    trigger_type: str
    resolution: str
    width: int | None
    height: int | None
    priority: str
    seq: int | None
    sensor_snapshot: dict[str, Any] | None
    local_analysis: dict[str, Any] | None
    server_analysis: dict[str, Any] | None
    upload_state: str
    image_bytes: int

    @field_serializer("timestamp", "received_at")
    def _iso(self, dt: datetime) -> str | None:
        return iso_z(dt)


class CaptureList(BaseModel):
    items: list[CaptureOut]
    next_cursor: str | None
