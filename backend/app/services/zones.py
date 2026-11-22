"""Zonas de cultivo (Contrato §5.5): alta, cambios y la ventana de luz como nivel de config."""

import logging
from typing import Any

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.errors import ApiError
from app.models.config import ConfigRevision
from app.models.node import Node
from app.models.zone import Zone
from app.schemas.zone import ZoneCreate

log = logging.getLogger(__name__)


def zone_scope(zone_id: str) -> str:
    """Scope de config_revisions para los cambios de una zona."""
    return f"zone:{zone_id}"


async def node_ids(db: AsyncSession, zone_id: str) -> list[str]:
    q = select(Node.node_id).where(Node.zone_id == zone_id).order_by(Node.node_id)
    return list((await db.execute(q)).scalars())


async def view(db: AsyncSession, zone: Zone) -> dict[str, Any]:
    return {"zone_id": zone.zone_id, "name": zone.name, "grow_mode": zone.grow_mode,
            "light_cycle": zone.light_cycle, "node_ids": await node_ids(db, zone.zone_id),
            "created_at": zone.created_at, "updated_at": zone.updated_at}


async def list_zones(db: AsyncSession) -> list[dict[str, Any]]:
    zones = (await db.execute(select(Zone).order_by(Zone.zone_id))).scalars().all()
    return [await view(db, z) for z in zones]


async def get_zone(db: AsyncSession, zone_id: str) -> Zone:
    zone = await db.get(Zone, zone_id)
    if zone is None:
        raise ApiError(404, f"No existe la zona {zone_id}")
    return zone


async def create_zone(db: AsyncSession, body: ZoneCreate, user: str) -> Zone:
    if await db.get(Zone, body.zone_id) is not None:
        raise ApiError(409, f"La zona {body.zone_id} ya existe")
    zone = Zone(zone_id=body.zone_id, name=body.name, grow_mode=body.grow_mode,
                light_cycle=body.light_cycle.model_dump() if body.light_cycle else None)
    db.add(zone)
    await db.commit()
    await db.refresh(zone)
    log.info("Zona %s creada por %s", zone.zone_id, user)
    return zone


async def patch_zone(
    db: AsyncSession, zone_id: str, patch: dict[str, Any], user: str
) -> tuple[Zone, int | None]:
    """Aplica un cambio parcial. Si cambia la ventana de luz devuelve la versión nueva."""
    zone = await get_zone(db, zone_id)
    if "name" in patch and patch["name"] is None:
        raise ApiError(422, "name no puede ser null",
                       detail=[{"field": "name", "error": "obligatorio"}])
    old_cycle = zone.light_cycle
    for key, value in patch.items():
        setattr(zone, key, value)
    rev = None
    if "light_cycle" in patch and patch["light_cycle"] != old_cycle:
        rev = ConfigRevision(scope=zone_scope(zone_id), changed_by=user,
                             changes={"light_cycle": patch["light_cycle"]})
        db.add(rev)
    await db.commit()
    await db.refresh(zone)
    return zone, rev.id if rev else None


async def delete_zone(db: AsyncSession, zone_id: str, user: str) -> None:
    zone = await get_zone(db, zone_id)
    nodes = await node_ids(db, zone_id)
    if nodes:
        raise ApiError(409, f"La zona {zone_id} tiene nodos asignados", detail={"node_ids": nodes})
    await db.delete(zone)
    await db.commit()
    log.info("Zona %s borrada por %s", zone_id, user)
