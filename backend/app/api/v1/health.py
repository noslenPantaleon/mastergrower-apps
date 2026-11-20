"""GET /api/v1/health — estado de la API y sus dependencias."""

from typing import Annotated, Literal

from fastapi import APIRouter, Depends, Request
from pydantic import BaseModel

from app import __version__
from app.core.db import ping_db

router = APIRouter(tags=["system"])

MqttStatus = Literal["ok", "error", "disabled"]


class HealthResponse(BaseModel):
    status: Literal["ok", "degraded"]
    version: str
    db: Literal["ok", "error"]
    mqtt: MqttStatus


async def db_status() -> bool:
    return await ping_db()


async def mqtt_status(request: Request) -> MqttStatus:
    ingestor = getattr(request.app.state, "ingestor", None)
    if ingestor is None:
        return "disabled"
    return "ok" if ingestor.connected else "error"


@router.get("/health", response_model=HealthResponse)
async def health(
    db_ok: Annotated[bool, Depends(db_status)],
    mqtt: Annotated[MqttStatus, Depends(mqtt_status)],
) -> HealthResponse:
    return HealthResponse(
        status="ok" if db_ok and mqtt != "error" else "degraded",
        version=__version__,
        db="ok" if db_ok else "error",
        mqtt=mqtt,
    )
