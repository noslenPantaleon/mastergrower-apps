"""Nodos: listado, alta con emisión de token y asignación de zona (Contrato §5.5,
contract/http/nodes.md)."""

from typing import Annotated

from fastapi import APIRouter, BackgroundTasks, Depends, Request, Response
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.db import get_session
from app.core.security import ReaderDep, UserDep
from app.models.node import Node
from app.mqtt.refresh import notify_refresh
from app.schemas.common import NodeKind, NodeState
from app.schemas.node import NodeCreate, NodeList, NodeOut, NodeRegistered, NodeTokenOut, NodeZoneIn
from app.services import nodes as svc

router = APIRouter(prefix="/nodes", tags=["nodes"])

DbDep = Annotated[AsyncSession, Depends(get_session)]


@router.get("", response_model=NodeList)
async def list_nodes(
    _: ReaderDep,
    db: DbDep,
    kind: NodeKind | None = None,
    zone_id: str | None = None,
    node_state: NodeState | None = None,
) -> NodeList:
    q = select(Node).order_by(Node.node_id)
    if kind:
        q = q.where(Node.kind == kind)
    if zone_id:
        q = q.where(Node.zone_id == zone_id)
    if node_state:
        q = q.where(Node.node_state == node_state)
    rows = (await db.execute(q)).scalars()
    return NodeList(items=[NodeOut.model_validate(n) for n in rows])


@router.post("", status_code=201, response_model=NodeRegistered)
async def register_node(
    body: NodeCreate, user: UserDep, db: DbDep, request: Request, bg: BackgroundTasks
) -> NodeRegistered:
    node, token, version = await svc.register(db, body, user.name)
    if version is not None and node.kind == "cam":
        bg.add_task(notify_refresh, request, [node.node_id], version)
    return NodeRegistered(node_id=node.node_id, token=token, node=NodeOut.model_validate(node))


@router.get("/{node_id}", response_model=NodeOut)
async def get_node(node_id: str, _: ReaderDep, db: DbDep) -> Node:
    return await svc.get_node(db, node_id)


@router.put("/{node_id}/zone", response_model=NodeOut)
async def put_zone(
    node_id: str, body: NodeZoneIn, user: UserDep, db: DbDep, request: Request,
    bg: BackgroundTasks,
) -> Node:
    node, version = await svc.assign_zone(db, node_id, body.zone_id, user.name)
    if version is not None and node.kind == "cam":
        bg.add_task(notify_refresh, request, [node_id], version)
    return node


@router.post("/{node_id}/token", status_code=201, response_model=NodeTokenOut)
async def rotate_token(node_id: str, user: UserDep, db: DbDep) -> NodeTokenOut:
    return NodeTokenOut(node_id=node_id, token=await svc.rotate_token(db, node_id, user.name))


@router.delete("/{node_id}/token", status_code=204)
async def revoke_token(node_id: str, user: UserDep, db: DbDep) -> Response:
    await svc.revoke(db, node_id, user.name)
    return Response(status_code=204)
