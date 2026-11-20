"""Capturas (Contrato §5.2, contract/http/captures.md)."""

import json
from datetime import datetime
from typing import Annotated

from fastapi import APIRouter, Depends, File, Form, Header, Query, Response, UploadFile
from pydantic import ValidationError
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.config import Settings, get_settings
from app.core.db import get_session
from app.core.errors import ApiError, validation_detail
from app.core.security import NodeDep, ReaderDep
from app.models.capture import Capture, parse_id
from app.schemas.capture import CaptureList, CaptureMetadata, CaptureOut, TriggerType
from app.services import captures as svc
from app.storage.image_store import ImageStore, get_image_store

router = APIRouter(prefix="/captures", tags=["captures"])

DbDep = Annotated[AsyncSession, Depends(get_session)]
StoreDep = Annotated[ImageStore, Depends(get_image_store)]
SettingsDep = Annotated[Settings, Depends(get_settings)]

IMMUTABLE_CACHE = {"Cache-Control": "private, max-age=86400, immutable"}


async def _read_limited(f: UploadFile, max_bytes: int) -> bytes:
    data = await f.read(max_bytes + 1)
    if len(data) > max_bytes:
        raise ApiError(413, f"La imagen supera {max_bytes} bytes")
    if not data:
        raise ApiError(400, "Imagen vacía")
    return data


@router.post("", status_code=201, response_model=CaptureOut,
             responses={200: {"description": "Ya existía (misma Idempotency-Key)"}})
async def upload_capture(
    response: Response,
    node: NodeDep,
    db: DbDep,
    store: StoreDep,
    settings: SettingsDep,
    image: Annotated[UploadFile, File(description="JPEG original")],
    metadata: Annotated[str, Form(description="JSON según capture_metadata.schema.json")],
    idempotency_key: Annotated[str | None, Header(max_length=64)] = None,
) -> Capture:
    try:
        meta = CaptureMetadata.model_validate(json.loads(metadata))
    except json.JSONDecodeError as e:
        raise ApiError(400, "metadata no es JSON válido", detail=str(e)) from e
    except ValidationError as e:
        raise ApiError(422, "metadata no cumple el contrato",
                       detail=validation_detail(list(e.errors()))) from e

    if meta.node_id != node.name:
        raise ApiError(403, f"El token es de {node.name}, no de {meta.node_id}")

    data = await _read_limited(image, settings.max_image_bytes)
    r = await svc.create_capture(db, store, meta, data, idempotency_key)
    if not r.created:
        response.status_code = 200
    return r.capture


@router.get("", response_model=CaptureList)
async def list_captures(
    _: ReaderDep,
    db: DbDep,
    node_id: str | None = None,
    trigger_type: TriggerType | None = None,
    since: datetime | None = None,
    until: datetime | None = None,
    zone_id: Annotated[str | None, Query(description="zona actual del nodo")] = None,
    limit: Annotated[int, Query(ge=1, le=200)] = 50,
    cursor: str | None = None,
) -> CaptureList:
    rows, next_cursor = await svc.list_captures(
        db, node_id=node_id, trigger_type=trigger_type, zone_id=zone_id, since=since, until=until,
        limit=limit, cursor=cursor,
    )
    return CaptureList(items=[CaptureOut.model_validate(r) for r in rows], next_cursor=next_cursor)


async def _get(db: AsyncSession, capture_id: str) -> Capture:
    n = parse_id(capture_id)
    c = await db.get(Capture, n) if n is not None else None
    if c is None:
        raise ApiError(404, f"No existe la captura {capture_id}")
    return c


@router.get("/{capture_id}", response_model=CaptureOut)
async def get_capture(capture_id: str, _: ReaderDep, db: DbDep) -> Capture:
    return await _get(db, capture_id)


@router.get("/{capture_id}/image", response_class=Response,
            responses={200: {"content": {"image/jpeg": {}}}})
async def get_image(capture_id: str, _: ReaderDep, db: DbDep, store: StoreDep) -> Response:
    c = await _get(db, capture_id)
    data = await store.read(c.image_path)
    if data is None:
        raise ApiError(404, "Imagen no disponible")
    return Response(data, media_type="image/jpeg", headers=IMMUTABLE_CACHE)


@router.get("/{capture_id}/thumb", response_class=Response,
            responses={200: {"content": {"image/jpeg": {}}}})
async def get_thumb(
    capture_id: str, _: ReaderDep, db: DbDep, store: StoreDep, settings: SettingsDep
) -> Response:
    c = await _get(db, capture_id)
    data = await svc.get_thumb(store, c, settings.thumb_max_px)
    return Response(data, media_type="image/jpeg", headers=IMMUTABLE_CACHE)
