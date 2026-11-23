"""Esquemas HTTP de firmware (contract/http/firmware.md)."""

from datetime import datetime

from pydantic import BaseModel, ConfigDict, Field, field_serializer

from app.schemas.common import iso_z


class FirmwareOut(BaseModel):
    firmware_id: str
    kind: str
    version: str
    project: str
    idf_ver: str
    size: int
    sha256: str
    uploaded_by: str
    created_at: datetime
    download_url: str   # ruta relativa a la API (el nodo la resuelve con su URL base)

    @field_serializer("created_at")
    def _iso(self, dt: datetime) -> str | None:
        return iso_z(dt)


class FirmwareList(BaseModel):
    items: list[FirmwareOut]


class DeployIn(BaseModel):
    model_config = ConfigDict(extra="forbid")

    node_ids: list[str] = Field(min_length=1, max_length=32)


class DeployResult(BaseModel):
    node_id: str
    cmd_id: str | None
    sent: bool
    error: str | None = None


class DeployOut(BaseModel):
    firmware_id: str
    version: str
    results: list[DeployResult]
