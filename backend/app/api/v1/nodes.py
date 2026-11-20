"""Nodos, solo lectura en B2 (Contrato §5.5, contract/http/nodes.md). El alta llega en B4."""

from typing import Annotated

from fastapi import APIRouter, Depends
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.db import get_session
from app.core.errors import ApiError
from app.core.security import ReaderDep
from app.models.node import Node
from app.schemas.common import NodeKind, NodeState
from app.schemas.node import NodeList, NodeOut

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


@router.get("/{node_id}", response_model=NodeOut)
async def get_node(node_id: str, _: ReaderDep, db: DbDep) -> Node:
    node = await db.get(Node, node_id)
    if node is None:
        raise ApiError(404, f"No existe el nodo {node_id}")
    return node
