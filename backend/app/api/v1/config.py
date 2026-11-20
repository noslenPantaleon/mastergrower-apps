"""Configuración (Contrato §5.4 y §7, contract/http/config.md)."""

import logging
from typing import Annotated, Any

from fastapi import APIRouter, BackgroundTasks, Body, Depends, Header, Request, Response
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.db import get_session
from app.core.errors import ApiError
from app.core.security import ReaderDep, UserDep
from app.models.node import Node
from app.mqtt.ingestor import command
from app.schemas.config import GlobalConfigOut, NodeConfigOut, NodeOverrideOut
from app.services import config as svc

log = logging.getLogger(__name__)

router = APIRouter(prefix="/config", tags=["config"])

DbDep = Annotated[AsyncSession, Depends(get_session)]
Patch = Annotated[dict[str, Any], Body(description="parámetros a cambiar; null = borrar",
                                        examples=[{"capture_interval_min": 60}])]


async def _notify(request: Request, node_ids: list[str], version: int) -> None:
    """Publica refresh_config a cada nodo (mejor esfuerzo; el nodo también consulta solo)."""
    ingestor = getattr(request.app.state, "ingestor", None)
    if ingestor is None:
        return
    sent = 0
    for nid in node_ids:
        msg = command(nid, "refresh_config", f"cfg-{version}-{nid}", {"version": version})
        sent += await ingestor.publish(f"growlab/cam/{nid}/cmd", msg)
    log.info("Config v%d: refresh_config enviado a %d/%d nodos", version, sent, len(node_ids))


async def _cam_nodes(db: AsyncSession) -> list[str]:
    q = select(Node.node_id).where(Node.kind == "cam").order_by(Node.node_id)
    return list((await db.execute(q)).scalars())


def _etag(version: int) -> str:
    return f'"{version}"'


# --- global ---

@router.get("/global", response_model=GlobalConfigOut)
async def get_global(_: ReaderDep, db: DbDep) -> dict[str, Any]:
    return await svc.global_view(db)


@router.put("/global", response_model=GlobalConfigOut)
async def put_global(
    patch: Patch, user: UserDep, db: DbDep, request: Request, bg: BackgroundTasks
) -> dict[str, Any]:
    version = await svc.update_global(db, patch, user.name)
    if version is not None:
        bg.add_task(_notify, request, await _cam_nodes(db), version)
    return await svc.global_view(db)


# --- por nodo ---

@router.get("/nodes/{node_id}", response_model=NodeConfigOut,
            responses={304: {"description": "Sin cambios desde la versión de If-None-Match"}})
async def get_node_config(
    node_id: str,
    who: ReaderDep,
    db: DbDep,
    response: Response,
    if_none_match: Annotated[str | None, Header()] = None,
) -> Any:
    if who.kind == "node" and who.name != node_id:
        raise ApiError(403, f"El token es de {who.name}, no de {node_id}")
    view = await svc.node_view(db, node_id)
    etag = _etag(view["version"])
    if if_none_match and etag in [t.strip().removeprefix("W/") for t in if_none_match.split(",")]:
        return Response(status_code=304, headers={"ETag": etag})
    response.headers["ETag"] = etag
    return view


@router.get("/nodes/{node_id}/override", response_model=NodeOverrideOut)
async def get_override(node_id: str, _: ReaderDep, db: DbDep) -> dict[str, Any]:
    return await svc.override_view(db, node_id)


@router.put("/nodes/{node_id}/override", response_model=NodeConfigOut)
async def put_override(
    node_id: str, patch: Patch, user: UserDep, db: DbDep, request: Request,
    bg: BackgroundTasks, response: Response,
) -> dict[str, Any]:
    version = await svc.update_override(db, node_id, patch, user.name)
    if version is not None and svc.node_kind(node_id) == "cam":
        bg.add_task(_notify, request, [node_id], version)
    view = await svc.node_view(db, node_id)
    response.headers["ETag"] = _etag(view["version"])
    return view


@router.delete("/nodes/{node_id}/override", status_code=204)
async def delete_override(
    node_id: str, user: UserDep, db: DbDep, request: Request, bg: BackgroundTasks
) -> Response:
    version = await svc.delete_override(db, node_id, user.name)
    if version is not None and svc.node_kind(node_id) == "cam":
        bg.add_task(_notify, request, [node_id], version)
    return Response(status_code=204)
