"""Firmware para OTA: la API lee versión y proyecto del propio .bin (esp_app_desc_t), lo guarda y
manda ota_update a los nodos (contract/http/firmware.md)."""

import hashlib
import logging
import struct
from dataclasses import dataclass
from typing import Any

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.errors import ApiError
from app.models.firmware import Firmware, format_firmware_id, parse_firmware_id
from app.storage.image_store import ImageStore

log = logging.getLogger(__name__)

# Proyecto de ESP-IDF → tipo de nodo. Un .bin de otro proyecto se rechaza (no se puede mandar el
# firmware de la pantalla a una cámara).
PROJECT_KIND = {"camera_node": "cam", "display_node": "disp", "control_node": "ctrl"}

IMAGE_MAGIC = 0xE9
APP_DESC_MAGIC = 0xABCD5432
APP_DESC_OFFSET = 24 + 8  # esp_image_header_t + esp_image_segment_header_t


@dataclass
class AppDesc:
    version: str
    project: str
    idf_ver: str


def _cstr(b: bytes) -> str:
    return b.split(b"\0", 1)[0].decode("utf-8", "replace")


def parse_app_desc(data: bytes) -> AppDesc:
    """esp_app_desc_t del primer segmento de una imagen de app de ESP-IDF."""
    if len(data) < APP_DESC_OFFSET + 256 or data[0] != IMAGE_MAGIC:
        raise ApiError(422, "No es una imagen de app de ESP-IDF",
                       detail=[{"field": "file", "error": "falta la cabecera 0xE9"}])
    d = data[APP_DESC_OFFSET:APP_DESC_OFFSET + 256]
    (magic,) = struct.unpack_from("<I", d, 0)
    if magic != APP_DESC_MAGIC:
        raise ApiError(422, "La imagen no tiene esp_app_desc_t",
                       detail=[{"field": "file", "error": "magic de app_desc inválido"}])
    # magic(4) secure_version(4) reserv1(8) version[32] project_name[32] time[16] date[16]
    # idf_ver[32]
    return AppDesc(version=_cstr(d[16:48]), project=_cstr(d[48:80]), idf_ver=_cstr(d[112:144]))


def view(fw: Firmware, api_prefix: str) -> dict[str, Any]:
    fid = format_firmware_id(fw.id)
    return {"firmware_id": fid, "kind": fw.kind, "version": fw.version, "project": fw.project,
            "idf_ver": fw.idf_ver, "size": fw.size, "sha256": fw.sha256,
            "uploaded_by": fw.uploaded_by, "created_at": fw.created_at,
            "download_url": f"{api_prefix}/firmware/{fid}/download"}


async def create(db: AsyncSession, store: ImageStore, data: bytes, user: str) -> Firmware:
    desc = parse_app_desc(data)
    kind = PROJECT_KIND.get(desc.project)
    if kind is None:
        valid = ", ".join(sorted(PROJECT_KIND))
        raise ApiError(422, f"Proyecto desconocido: {desc.project}",
                       detail=[{"field": "file", "error": f"proyectos válidos: {valid}"}])
    if not desc.version:
        raise ApiError(422, "La imagen no declara versión")
    exists = await db.execute(select(Firmware.id).where(Firmware.kind == kind,
                                                         Firmware.version == desc.version))
    if exists.first():
        raise ApiError(409, f"Ya existe {kind} {desc.version}")
    path = f"{kind}/{desc.version}.bin"
    await store.save(path, data)
    fw = Firmware(kind=kind, version=desc.version, project=desc.project, idf_ver=desc.idf_ver,
                  size=len(data), sha256=hashlib.sha256(data).hexdigest(), path=path,
                  uploaded_by=user)
    db.add(fw)
    await db.commit()
    await db.refresh(fw)
    log.info("Firmware %s %s (%d bytes) subido por %s", kind, desc.version, len(data), user)
    return fw


async def get(db: AsyncSession, firmware_id: str) -> Firmware:
    n = parse_firmware_id(firmware_id)
    fw = await db.get(Firmware, n) if n is not None else None
    if fw is None:
        raise ApiError(404, f"No existe el firmware {firmware_id}")
    return fw


async def list_(db: AsyncSession, kind: str | None) -> list[Firmware]:
    q = select(Firmware).order_by(Firmware.created_at.desc(), Firmware.id.desc())
    if kind:
        q = q.where(Firmware.kind == kind)
    return list((await db.execute(q)).scalars())


async def latest(db: AsyncSession, kind: str) -> Firmware:
    items = await list_(db, kind)
    if not items:
        raise ApiError(404, f"No hay firmware para {kind}")
    return items[0]
