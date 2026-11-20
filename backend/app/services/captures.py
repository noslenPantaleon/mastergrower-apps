"""Lógica de capturas: alta idempotente, listado paginado por cursor y miniaturas."""

import asyncio
import io
import logging
from dataclasses import dataclass
from datetime import UTC, datetime

from PIL import Image, UnidentifiedImageError
from sqlalchemy import and_, or_, select
from sqlalchemy.exc import IntegrityError
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.errors import ApiError
from app.models.capture import Capture, format_id
from app.schemas.capture import CaptureMetadata
from app.storage.image_store import ImageStore

log = logging.getLogger(__name__)

JPEG_SOI = b"\xff\xd8\xff"


@dataclass
class CreateResult:
    capture: Capture
    created: bool  # False = ya existía (reintento con la misma Idempotency-Key)


async def _existing(db: AsyncSession, node_id: str, local_id: str | None) -> Capture | None:
    if not local_id:
        return None
    q = select(Capture).where(Capture.node_id == node_id, Capture.local_id == local_id)
    return (await db.execute(q)).scalar_one_or_none()


def _aware(dt: datetime) -> datetime:
    return dt if dt.tzinfo else dt.replace(tzinfo=UTC)


def image_path(c: Capture) -> str:
    return f"{c.node_id}/{_aware(c.timestamp):%Y/%m/%d}/{format_id(c.id)}.jpg"


def thumb_path(path: str) -> str:
    return path.removesuffix(".jpg") + "_thumb.jpg"


async def create_capture(
    db: AsyncSession,
    store: ImageStore,
    meta: CaptureMetadata,
    image: bytes,
    local_id: str | None,
) -> CreateResult:
    if not image.startswith(JPEG_SOI):
        raise ApiError(415, "La imagen no es un JPEG")

    if prev := await _existing(db, meta.node_id, local_id):
        return CreateResult(prev, created=False)

    now = datetime.now(UTC)
    c = Capture(
        node_id=meta.node_id,
        timestamp=meta.timestamp or now,
        timestamp_approx=meta.timestamp is None,
        received_at=now,
        camera_type=meta.camera_type,
        trigger_type=meta.trigger_type,
        resolution=meta.resolution,
        width=meta.width,
        height=meta.height,
        priority=meta.priority,
        seq=meta.seq,
        sensor_snapshot=meta.sensor_snapshot.model_dump() if meta.sensor_snapshot else None,
        local_analysis=meta.local_analysis,
        upload_state="received",
        image_path="",
        image_bytes=len(image),
        local_id=local_id,
    )
    db.add(c)
    try:
        await db.flush()  # asigna el id (y choca acá si otro pedido ganó la misma clave)
    except IntegrityError:
        await db.rollback()
        if prev := await _existing(db, meta.node_id, local_id):
            return CreateResult(prev, created=False)
        raise

    c.image_path = image_path(c)
    try:
        await store.save(c.image_path, image)
        await db.commit()
    except BaseException:
        await db.rollback()
        await store.delete(c.image_path)
        raise
    log.info("captura %s de %s (%d bytes)", c.capture_id, c.node_id, len(image))
    return CreateResult(c, created=True)


def _cursor(c: Capture) -> str:
    return f"{int(_aware(c.timestamp).timestamp() * 1_000_000)}_{c.id}"


def _parse_cursor(cursor: str) -> tuple[datetime, int]:
    try:
        us, _, cid = cursor.partition("_")
        return datetime.fromtimestamp(int(us) / 1_000_000, UTC), int(cid)
    except ValueError as e:
        raise ApiError(400, "Cursor inválido") from e


async def list_captures(
    db: AsyncSession,
    *,
    node_id: str | None,
    trigger_type: str | None,
    since: datetime | None,
    until: datetime | None,
    limit: int,
    cursor: str | None,
) -> tuple[list[Capture], str | None]:
    q = select(Capture)
    if node_id:
        q = q.where(Capture.node_id == node_id)
    if trigger_type:
        q = q.where(Capture.trigger_type == trigger_type)
    if since:
        q = q.where(Capture.timestamp >= since)
    if until:
        q = q.where(Capture.timestamp < until)
    if cursor:
        cts, cid = _parse_cursor(cursor)
        q = q.where(or_(Capture.timestamp < cts, and_(Capture.timestamp == cts, Capture.id < cid)))
    q = q.order_by(Capture.timestamp.desc(), Capture.id.desc()).limit(limit + 1)
    rows = list((await db.execute(q)).scalars())
    next_cursor = _cursor(rows[limit - 1]) if len(rows) > limit else None
    return rows[:limit], next_cursor


def _thumbnail(data: bytes, max_px: tuple[int, int]) -> bytes:
    with Image.open(io.BytesIO(data)) as im:
        im.draft("RGB", max_px)  # el decodificador JPEG escala al leer: mucho más rápido
        im = im.convert("RGB")
        im.thumbnail(max_px)
        out = io.BytesIO()
        im.save(out, "JPEG", quality=80, optimize=True)
        return out.getvalue()


async def get_thumb(store: ImageStore, c: Capture, max_px: tuple[int, int]) -> bytes:
    path = thumb_path(c.image_path)
    if (cached := await store.read(path)) is not None:
        return cached
    original = await store.read(c.image_path)
    if original is None:
        raise ApiError(404, "Imagen no disponible")
    try:
        thumb = await asyncio.to_thread(_thumbnail, original, max_px)
    except (UnidentifiedImageError, OSError) as e:
        raise ApiError(422, "No se pudo decodificar la imagen") from e
    await store.save(path, thumb)
    return thumb
