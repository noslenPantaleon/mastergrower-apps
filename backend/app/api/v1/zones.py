"""Zonas de cultivo (Contrato §5.5, contract/http/zones.md)."""

from typing import Annotated, Any

from fastapi import APIRouter, BackgroundTasks, Depends, Request, Response
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.db import get_session
from app.core.security import ReaderDep, UserDep
from app.mqtt.refresh import cam_nodes, notify_refresh
from app.schemas.zone import ZoneCreate, ZoneList, ZoneOut, ZonePatch
from app.services import zones as svc

router = APIRouter(prefix="/zones", tags=["zones"])

DbDep = Annotated[AsyncSession, Depends(get_session)]


@router.get("", response_model=ZoneList)
async def list_zones(_: ReaderDep, db: DbDep) -> dict[str, Any]:
    return {"items": await svc.list_zones(db)}


@router.post("", status_code=201, response_model=ZoneOut)
async def create_zone(body: ZoneCreate, user: UserDep, db: DbDep) -> dict[str, Any]:
    return await svc.view(db, await svc.create_zone(db, body, user.name))


@router.get("/{zone_id}", response_model=ZoneOut)
async def get_zone(zone_id: str, _: ReaderDep, db: DbDep) -> dict[str, Any]:
    return await svc.view(db, await svc.get_zone(db, zone_id))


@router.patch("/{zone_id}", response_model=ZoneOut)
async def patch_zone(
    zone_id: str, body: ZonePatch, user: UserDep, db: DbDep, request: Request,
    bg: BackgroundTasks,
) -> dict[str, Any]:
    patch = body.model_dump(mode="json", exclude_unset=True)
    zone, version = await svc.patch_zone(db, zone_id, patch, user.name)
    if version is not None:
        bg.add_task(notify_refresh, request, await cam_nodes(db, zone_id), version)
    return await svc.view(db, zone)


@router.delete("/{zone_id}", status_code=204)
async def delete_zone(zone_id: str, user: UserDep, db: DbDep) -> Response:
    await svc.delete_zone(db, zone_id, user.name)
    return Response(status_code=204)
