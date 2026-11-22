"""Alta de nodos, tokens por nodo y asignación de zona (Contrato §5.5 y §8.2)."""

import hashlib
import logging
import secrets
from datetime import UTC, datetime

from sqlalchemy import select, update
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.errors import ApiError
from app.models.config import ConfigRevision
from app.models.node import Node
from app.models.node_token import NodeToken
from app.models.zone import Zone
from app.schemas.node import NodeCreate

log = logging.getLogger(__name__)


def hash_token(token: str) -> str:
    return hashlib.sha256(token.encode()).hexdigest()


def new_token() -> str:
    return secrets.token_urlsafe(32)  # 256 bits: un hash rápido alcanza, no hace falta bcrypt


async def node_for_token(db: AsyncSession, token: str) -> str | None:
    """node_id dueño del token, si está vigente."""
    q = select(NodeToken.node_id).where(
        NodeToken.token_hash == hash_token(token), NodeToken.revoked_at.is_(None))
    return (await db.execute(q)).scalar_one_or_none()


async def _active_tokens(db: AsyncSession, node_id: str) -> int:
    q = select(NodeToken.id).where(NodeToken.node_id == node_id, NodeToken.revoked_at.is_(None))
    return len((await db.execute(q)).all())


async def _get_or_create(db: AsyncSession, node_id: str) -> Node:
    node = await db.get(Node, node_id)
    if node is None:
        node = Node(node_id=node_id, kind=node_id.split("-", 1)[0])
        db.add(node)
    return node


async def import_env_tokens(db: AsyncSession, tokens: dict[str, str]) -> int:
    """Pasa a la base los tokens de API_NODE_TOKENS (los que ya tienen grabados los nodos).

    Solo importa un token si nunca se vio (ni siquiera revocado) y si el nodo no tiene otro
    vigente: revocar o rotar desde la API no se deshace al reiniciar.
    """
    imported = 0
    for node_id, token in tokens.items():
        h = hash_token(token)
        if (await db.execute(select(NodeToken.id).where(NodeToken.token_hash == h))).first():
            continue
        if await _active_tokens(db, node_id):
            log.warning("API_NODE_TOKENS: %s ya tiene un token vigente en la base; se ignora el "
                        "de .env", node_id)
            continue
        node = await _get_or_create(db, node_id)
        node.registered_at = node.registered_at or datetime.now(UTC)
        db.add(NodeToken(node_id=node_id, token_hash=h, source="env", created_by="env"))
        imported += 1
    if imported:
        await db.commit()
        log.info("Tokens de nodo importados desde API_NODE_TOKENS: %d", imported)
    return imported


async def _check_zone(db: AsyncSession, zone_id: str | None) -> None:
    if zone_id is not None and await db.get(Zone, zone_id) is None:
        raise ApiError(422, f"No existe la zona {zone_id}",
                       detail=[{"field": "zone_id", "error": "zona inexistente"}])


def _zone_revision(db: AsyncSession, node: Node, zone_id: str | None, user: str) -> ConfigRevision:
    """Cambiar de zona cambia la config efectiva (ventana de luz): nueva versión del nodo."""
    node.zone_id = zone_id
    rev = ConfigRevision(scope=node.node_id, changed_by=user, changes={"zone_id": zone_id})
    db.add(rev)
    return rev


async def register(db: AsyncSession, body: NodeCreate, user: str) -> tuple[Node, str, int | None]:
    """Da de alta (o completa) un nodo y emite su token. Devuelve (nodo, token, versión)."""
    kind = body.node_id.split("-", 1)[0]
    if body.camera_type and kind != "cam":
        raise ApiError(422, "camera_type solo aplica a nodos cam",
                       detail=[{"field": "camera_type", "error": "solo para cam-NN"}])
    if await _active_tokens(db, body.node_id):
        raise ApiError(409, f"{body.node_id} ya está dado de alta; para un token nuevo usar "
                       f"POST /nodes/{body.node_id}/token")
    await _check_zone(db, body.zone_id)

    node = await _get_or_create(db, body.node_id)
    if body.camera_type:
        node.camera_type = body.camera_type
    node.registered_at = datetime.now(UTC)
    rev = None
    if body.zone_id is not None and body.zone_id != node.zone_id:
        rev = _zone_revision(db, node, body.zone_id, user)
    token = new_token()
    db.add(NodeToken(node_id=node.node_id, token_hash=hash_token(token), source="api",
                     created_by=user))
    await db.commit()
    log.info("Alta de %s por %s", node.node_id, user)
    return node, token, rev.id if rev else None


async def get_node(db: AsyncSession, node_id: str) -> Node:
    node = await db.get(Node, node_id)
    if node is None:
        raise ApiError(404, f"No existe el nodo {node_id}")
    return node


async def revoke_tokens(db: AsyncSession, node_id: str) -> int:
    r = await db.execute(update(NodeToken)
                         .where(NodeToken.node_id == node_id, NodeToken.revoked_at.is_(None))
                         .values(revoked_at=datetime.now(UTC)))
    return r.rowcount or 0


async def rotate_token(db: AsyncSession, node_id: str, user: str) -> str:
    """Revoca los tokens vigentes del nodo y emite uno nuevo."""
    node = await get_node(db, node_id)
    revoked = await revoke_tokens(db, node_id)
    token = new_token()
    db.add(NodeToken(node_id=node_id, token_hash=hash_token(token), source="api",
                     created_by=user))
    node.registered_at = node.registered_at or datetime.now(UTC)
    await db.commit()
    log.info("Token nuevo para %s por %s (%d revocados)", node_id, user, revoked)
    return token


async def revoke(db: AsyncSession, node_id: str, user: str) -> None:
    await get_node(db, node_id)
    revoked = await revoke_tokens(db, node_id)
    await db.commit()
    log.info("Tokens de %s revocados por %s: %d", node_id, user, revoked)


async def assign_zone(
    db: AsyncSession, node_id: str, zone_id: str | None, user: str
) -> tuple[Node, int | None]:
    node = await get_node(db, node_id)
    await _check_zone(db, zone_id)
    if node.zone_id == zone_id:
        return node, None
    rev = _zone_revision(db, node, zone_id, user)
    await db.commit()
    log.info("%s → zona %s (por %s)", node_id, zone_id or "-", user)
    return node, rev.id
