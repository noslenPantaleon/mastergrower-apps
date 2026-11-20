"""Autenticación por token (Contrato §5.1 y §8.2).

- Token de nodo: lo usa cada nodo para subir sus capturas (solo con su propio node_id).
- Token de usuario: pantalla, app y navegador; habilita las lecturas.
"""

import hmac
from dataclasses import dataclass
from typing import Annotated, Literal

from fastapi import Depends, Query, Request

from app.core.config import Settings, get_settings
from app.core.errors import ApiError


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


def _resolve(token: str | None, settings: Settings) -> Principal | None:
    if not token:
        return None
    if node := _lookup(token, settings.node_tokens):
        return Principal("node", node)
    if user := _lookup(token, settings.user_tokens):
        return Principal("user", user)
    return None


SettingsDep = Annotated[Settings, Depends(get_settings)]


async def require_node(request: Request, settings: SettingsDep) -> Principal:
    p = _resolve(_bearer(request), settings)
    if p is None:
        raise ApiError(401, "Token ausente o inválido")
    if p.kind != "node":
        raise ApiError(403, "Se requiere un token de nodo")
    return p


async def require_reader(
    request: Request,
    settings: SettingsDep,
    token: Annotated[str | None, Query(description="alternativa al encabezado, para <img>")] = None,
) -> Principal:
    p = _resolve(_bearer(request) or token, settings)
    if p is None:
        raise ApiError(401, "Token ausente o inválido")
    return p  # un nodo también puede leer (p. ej. para depurar); B4 afina permisos


NodeDep = Annotated[Principal, Depends(require_node)]
ReaderDep = Annotated[Principal, Depends(require_reader)]
