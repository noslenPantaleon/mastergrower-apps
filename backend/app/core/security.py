"""Autenticación por token (Contrato §5.1 y §8.2).

- Token de nodo: lo usa cada nodo para subir sus capturas (solo con su propio node_id). Se
  emite con POST /nodes y vive en la tabla node_tokens (hash); revocable (B4).
- Token de usuario: pantalla, app y navegador; habilita las lecturas. Por ahora en el entorno
  (API_USER_TOKENS).
"""

import hmac
from dataclasses import dataclass
from typing import Annotated, Literal

from fastapi import Depends, Query, Request
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.config import Settings, get_settings
from app.core.db import get_session
from app.core.errors import ApiError
from app.services.nodes import node_for_token


@dataclass(frozen=True)
class Principal:
    kind: Literal["node", "user"]
    name: str  # node_id o nombre de usuario


def _lookup(token: str, table: dict[str, str]) -> str | None:
    found = None
    for name, secret in table.items():
        # compare_digest en todos: no corta antes y no filtra por tiempo cuál coincidió
        if hmac.compare_digest(token.encode(), secret.encode()):
            found = name
    return found


def _bearer(request: Request) -> str | None:
    scheme, _, value = request.headers.get("authorization", "").partition(" ")
    if scheme.lower() == "bearer" and value.strip():
        return value.strip()
    return None


async def _resolve(token: str | None, settings: Settings, db: AsyncSession) -> Principal | None:
    if not token:
        return None
    if user := _lookup(token, settings.user_tokens):
        return Principal("user", user)
    if node := await node_for_token(db, token):
        return Principal("node", node)
    return None


SettingsDep = Annotated[Settings, Depends(get_settings)]
DbDep = Annotated[AsyncSession, Depends(get_session)]


async def require_node(request: Request, settings: SettingsDep, db: DbDep) -> Principal:
    p = await _resolve(_bearer(request), settings, db)
    if p is None:
        raise ApiError(401, "Token ausente o inválido")
    if p.kind != "node":
        raise ApiError(403, "Se requiere un token de nodo")
    return p


async def require_reader(
    request: Request,
    settings: SettingsDep,
    db: DbDep,
    token: Annotated[str | None, Query(description="alternativa al encabezado, para <img>")] = None,
) -> Principal:
    p = await _resolve(_bearer(request) or token, settings, db)
    if p is None:
        raise ApiError(401, "Token ausente o inválido")
    return p  # un nodo también puede leer (p. ej. para depurar)


async def require_user(request: Request, settings: SettingsDep, db: DbDep) -> Principal:
    p = await _resolve(_bearer(request), settings, db)
    if p is None:
        raise ApiError(401, "Token ausente o inválido")
    if p.kind != "user":
        raise ApiError(403, "Se requiere un token de usuario")
    return p


NodeDep = Annotated[Principal, Depends(require_node)]
UserDep = Annotated[Principal, Depends(require_user)]
ReaderDep = Annotated[Principal, Depends(require_reader)]
