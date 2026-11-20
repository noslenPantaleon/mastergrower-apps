"""GET /api/v1/health — estado de la API y sus dependencias."""

from typing import Annotated, Literal

from fastapi import APIRouter, Depends
from pydantic import BaseModel

from app import __version__
from app.core.db import ping_db

router = APIRouter(tags=["system"])


class HealthResponse(BaseModel):
    status: Literal["ok", "degraded"]
    version: str
    db: Literal["ok", "error"]


async def db_status() -> bool:
    return await ping_db()


@router.get("/health", response_model=HealthResponse)
async def health(db_ok: Annotated[bool, Depends(db_status)]) -> HealthResponse:
    return HealthResponse(
        status="ok" if db_ok else "degraded",
        version=__version__,
        db="ok" if db_ok else "error",
    )
