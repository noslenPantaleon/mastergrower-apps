"""Firmware y OTA (contract/http/firmware.md)."""

from typing import Annotated, Any

from fastapi import APIRouter, Depends, File, Request, Response, UploadFile
from sqlalchemy import delete
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.config import Settings, get_settings
from app.core.db import get_session
from app.core.errors import ApiError
from app.core.security import ReaderDep, UserDep
from app.models.firmware import Firmware
from app.models.node import Node
from app.schemas.common import NodeKind
from app.schemas.firmware import DeployIn, DeployOut, FirmwareList, FirmwareOut
from app.services import firmware as svc
from app.storage.image_store import ImageStore, LocalImageStore

router = APIRouter(prefix="/firmware", tags=["firmware"])

DbDep = Annotated[AsyncSession, Depends(get_session)]
SettingsDep = Annotated[Settings, Depends(get_settings)]


def get_firmware_store(settings: SettingsDep) -> ImageStore:
    return LocalImageStore(settings.firmware_dir)


StoreDep = Annotated[ImageStore, Depends(get_firmware_store)]


@router.post("", status_code=201, response_model=FirmwareOut)
async def upload(
    user: UserDep, db: DbDep, store: StoreDep, settings: SettingsDep,
    file: Annotated[UploadFile, File(description="imagen de app (build/<proyecto>.bin)")],
) -> dict[str, Any]:
    data = await file.read(settings.firmware_max_bytes + 1)
    if len(data) > settings.firmware_max_bytes:
        raise ApiError(413, f"El firmware supera {settings.firmware_max_bytes} bytes")
    fw = await svc.create(db, store, data, user.name)
    return svc.view(fw, settings.api_prefix)


@router.get("", response_model=FirmwareList)
async def list_firmware(
    _: ReaderDep, db: DbDep, settings: SettingsDep, kind: NodeKind | None = None,
) -> dict[str, Any]:
    return {"items": [svc.view(f, settings.api_prefix) for f in await svc.list_(db, kind)]}


@router.get("/{kind}/latest", response_model=FirmwareOut)
async def latest(kind: NodeKind, _: ReaderDep, db: DbDep, settings: SettingsDep) -> dict[str, Any]:
    return svc.view(await svc.latest(db, kind), settings.api_prefix)


@router.get("/{firmware_id}/download", response_class=Response,
            responses={200: {"content": {"application/octet-stream": {}}}})
async def download(firmware_id: str, _: ReaderDep, db: DbDep, store: StoreDep) -> Response:
    fw = await svc.get(db, firmware_id)
    data = await store.read(fw.path)
    if data is None:
        raise ApiError(404, f"Falta el archivo de {firmware_id}")
    return Response(data, media_type="application/octet-stream",
                    headers={"X-Firmware-Version": fw.version, "X-Firmware-Sha256": fw.sha256})


@router.post("/{firmware_id}/deploy", response_model=DeployOut)
async def deploy(
    firmware_id: str, body: DeployIn, user: UserDep, db: DbDep, settings: SettingsDep,
    request: Request,
) -> dict[str, Any]:
    fw = await svc.get(db, firmware_id)
    hub = request.app.state.hub  # el mismo emisor de comandos que usa el stream
    results = []
    for nid in body.node_ids:
        node = await db.get(Node, nid)
        if node is None or node.kind != fw.kind:
            results.append({"node_id": nid, "cmd_id": None, "sent": False,
                            "error": "not_found" if node is None else "wrong_kind"})
            continue
        if node.node_state in (None, "offline"):
            results.append({"node_id": nid, "cmd_id": None, "sent": False, "error": "node_offline"})
            continue
        cmd_id = f"ota-{fw.id}-{nid}"
        params = {"url": svc.view(fw, settings.api_prefix)["download_url"], "version": fw.version,
                  "sha256": fw.sha256, "size": fw.size}
        sent = hub.send_cmd is not None and await hub.send_cmd(nid, "ota_update", cmd_id, params)
        results.append({"node_id": nid, "cmd_id": cmd_id, "sent": sent,
                        "error": None if sent else "broker_unavailable"})
    return {"firmware_id": firmware_id, "version": fw.version, "results": results}


@router.delete("/{firmware_id}", status_code=204)
async def remove(firmware_id: str, user: UserDep, db: DbDep, store: StoreDep) -> Response:
    fw = await svc.get(db, firmware_id)
    await store.delete(fw.path)
    await db.execute(delete(Firmware).where(Firmware.id == fw.id))
    await db.commit()
    return Response(status_code=204)
