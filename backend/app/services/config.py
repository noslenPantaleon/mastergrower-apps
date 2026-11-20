"""Resolución override → global → default, cambios con revisión y versión por nodo."""

import re
from dataclasses import dataclass
from datetime import datetime
from typing import Any

from pydantic import BaseModel
from sqlalchemy import delete, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.errors import ApiError
from app.models.config import GLOBAL_SCOPE, ConfigGlobal, ConfigNode, ConfigRevision
from app.schemas.config import PARAMS_BY_KIND, defaults, validate_patch

NODE_ID_RE = re.compile(r"^(cam|ctrl|disp)-[0-9]{2,}$")


def node_kind(node_id: str) -> str:
    m = NODE_ID_RE.match(node_id)
    if not m:
        raise ApiError(404, f"node_id inválido: {node_id}")
    return m.group(1)


def _global_model() -> dict[str, type[BaseModel]]:
    """Clave → modelo, para todos los tipos de nodo (la global es común a todos)."""
    return {k: m for m in PARAMS_BY_KIND.values() for k in m.model_fields}


@dataclass
class Version:
    number: int
    at: datetime | None


async def _version(db: AsyncSession, scopes: list[str]) -> Version:
    q = select(ConfigRevision.id, ConfigRevision.created_at).where(
        ConfigRevision.scope.in_(scopes)).order_by(ConfigRevision.id.desc()).limit(1)
    row = (await db.execute(q)).first()
    return Version(row[0], row[1]) if row else Version(0, None)


async def _rows(db: AsyncSession, model, **where) -> dict[str, Any]:
    q = select(model.key, model.value).filter_by(**where)
    return {k: v for k, v in (await db.execute(q)).all()}


async def global_view(db: AsyncSession) -> dict[str, Any]:
    config: dict[str, Any] = {}
    for m in PARAMS_BY_KIND.values():
        config.update(defaults(m))
    sources = dict.fromkeys(config, "default")
    for k, v in (await _rows(db, ConfigGlobal)).items():
        if k in config:  # una clave vieja que ya no existe en el modelo se ignora
            config[k], sources[k] = v, "global"
    ver = await _version(db, [GLOBAL_SCOPE])
    return {"version": ver.number, "updated_at": ver.at, "config": config, "sources": sources}


async def node_view(db: AsyncSession, node_id: str) -> dict[str, Any]:
    model = PARAMS_BY_KIND.get(node_kind(node_id))
    config = defaults(model) if model else {}
    sources = dict.fromkeys(config, "default")
    for k, v in (await _rows(db, ConfigGlobal)).items():
        if k in config:
            config[k], sources[k] = v, "global"
    for k, v in (await _rows(db, ConfigNode, node_id=node_id)).items():
        if k in config:
            config[k], sources[k] = v, "override"
    ver = await _version(db, [GLOBAL_SCOPE, node_id])
    return {"node_id": node_id, "version": ver.number, "updated_at": ver.at,
            "config": config, "sources": sources}


async def override_view(db: AsyncSession, node_id: str) -> dict[str, Any]:
    node_kind(node_id)
    ver = await _version(db, [GLOBAL_SCOPE, node_id])
    return {"node_id": node_id, "version": ver.number, "updated_at": ver.at,
            "override": await _rows(db, ConfigNode, node_id=node_id)}


def _validate(models: dict[str, type[BaseModel]], patch: dict[str, Any]) -> dict[str, Any]:
    clean: dict[str, Any] = {}
    errors: list[dict[str, Any]] = []
    for key, value in patch.items():
        model = models.get(key)
        if model is None:
            errors.append({"field": key, "error": "parámetro desconocido"})
            continue
        ok, errs = validate_patch(model, {key: value})
        clean.update(ok)
        errors.extend(errs)
    if errors:
        raise ApiError(422, "La configuración no cumple el contrato", detail=errors)
    return clean


async def _apply(
    db: AsyncSession, table, scope: str, current: dict[str, Any], clean: dict[str, Any],
    user: str, **pk,
) -> int | None:
    """Aplica los cambios y crea una revisión. Devuelve la versión (None = sin cambios)."""
    changes = {k: v for k, v in clean.items()
               if (v is None and k in current) or (v is not None and current.get(k) != v)}
    if not changes:
        return None
    for k, v in changes.items():
        if v is None:
            await db.execute(delete(table).filter_by(key=k, **pk))
        elif k in current:
            row = await db.get(table, {"key": k, **pk})
            row.value = v
        else:
            db.add(table(key=k, value=v, **pk))
    rev = ConfigRevision(scope=scope, changed_by=user, changes=changes)
    db.add(rev)
    await db.commit()
    return rev.id


async def update_global(db: AsyncSession, patch: dict[str, Any], user: str) -> int | None:
    clean = _validate(_global_model(), patch)
    return await _apply(db, ConfigGlobal, GLOBAL_SCOPE, await _rows(db, ConfigGlobal), clean, user)


async def update_override(
    db: AsyncSession, node_id: str, patch: dict[str, Any], user: str
) -> int | None:
    model = PARAMS_BY_KIND.get(node_kind(node_id))
    models = dict.fromkeys(model.model_fields, model) if model else {}
    clean = _validate(models, patch)
    current = await _rows(db, ConfigNode, node_id=node_id)
    return await _apply(db, ConfigNode, node_id, current, clean, user, node_id=node_id)


async def delete_override(db: AsyncSession, node_id: str, user: str) -> int | None:
    node_kind(node_id)
    current = await _rows(db, ConfigNode, node_id=node_id)
    if not current:
        return None
    await db.execute(delete(ConfigNode).filter_by(node_id=node_id))
    rev = ConfigRevision(scope=node_id, changed_by=user, changes=dict.fromkeys(current))
    db.add(rev)
    await db.commit()
    return rev.id
