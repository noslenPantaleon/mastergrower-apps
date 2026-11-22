"""Aviso refresh_config a los nodos de cámara tras un cambio de configuración (B3/B4)."""

import logging

from fastapi import Request
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models.node import Node
from app.mqtt.ingestor import command

log = logging.getLogger(__name__)


async def notify_refresh(request: Request, node_ids: list[str], version: int) -> None:
    """Publica refresh_config a cada nodo (mejor esfuerzo; el nodo también consulta solo)."""
    ingestor = getattr(request.app.state, "ingestor", None)
    if ingestor is None or not node_ids:
        return
    sent = 0
    for nid in node_ids:
        msg = command(nid, "refresh_config", f"cfg-{version}-{nid}", {"version": version})
        sent += await ingestor.publish(f"growlab/cam/{nid}/cmd", msg)
    log.info("Config v%d: refresh_config enviado a %d/%d nodos", version, sent, len(node_ids))


async def cam_nodes(db: AsyncSession, zone_id: str | None = None) -> list[str]:
    """Nodos de cámara conocidos (todos, o los de una zona)."""
    q = select(Node.node_id).where(Node.kind == "cam").order_by(Node.node_id)
    if zone_id is not None:
        q = q.where(Node.zone_id == zone_id)
    return list((await db.execute(q)).scalars())
