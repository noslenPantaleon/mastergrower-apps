"""Esquemas HTTP de zonas (contract/http/zones.md)."""

from datetime import datetime

from pydantic import BaseModel, ConfigDict, Field, field_serializer

from app.schemas.common import GrowMode, iso_z
from app.schemas.config import LightWindow

ZONE_ID = r"^[a-z0-9][a-z0-9-]{0,23}$"


class ZoneCreate(BaseModel):
    model_config = ConfigDict(extra="forbid")

    zone_id: str = Field(pattern=ZONE_ID, examples=["zone-a"])
    name: str = Field(min_length=1, max_length=64)
    grow_mode: GrowMode | None = None
    light_cycle: LightWindow | None = None


class ZonePatch(BaseModel):
    """Parcial: solo los campos presentes; null borra grow_mode o light_cycle."""

    model_config = ConfigDict(extra="forbid")

    name: str | None = Field(None, min_length=1, max_length=64)
    grow_mode: GrowMode | None = None
    light_cycle: LightWindow | None = None


class ZoneOut(BaseModel):
    zone_id: str
    name: str
    grow_mode: str | None
    light_cycle: dict[str, str] | None
    node_ids: list[str]
    created_at: datetime
    updated_at: datetime

    @field_serializer("created_at", "updated_at")
    def _iso(self, dt: datetime) -> str | None:
        return iso_z(dt)


class ZoneList(BaseModel):
    items: list[ZoneOut]
