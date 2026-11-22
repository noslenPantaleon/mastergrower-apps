"""Vista en vivo (Contrato §5.6, contract/http/stream.md): sesiones y WebSockets."""

import contextlib
import logging
from typing import Annotated, Any

from fastapi import APIRouter, Depends, Query, Request, Response, WebSocket
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.config import Settings, get_settings
from app.core.db import get_session
from app.core.errors import ApiError
from app.core.security import ReaderDep, UserDep
from app.models.node import Node
from app.models.stream import StreamSession
from app.schemas.stream import RES_ORDER, StreamCreate, StreamCreated, StreamList, StreamOut
from app.services import config as config_svc
from app.services.nodes import node_for_token
from app.stream.hub import (
    WS_ENDED,
    WS_FORBIDDEN,
    WS_LIMIT,
    WS_UNAUTHORIZED,
    StreamHub,
)

log = logging.getLogger(__name__)

router = APIRouter(prefix="/stream", tags=["stream"])

DbDep = Annotated[AsyncSession, Depends(get_session)]
SettingsDep = Annotated[Settings, Depends(get_settings)]
DEFAULT_TTL_S = 120


def get_hub(request: Request) -> StreamHub:
    return request.app.state.hub


HubDep = Annotated[StreamHub, Depends(get_hub)]


def _row_view(r: StreamSession) -> dict[str, Any]:
    return {
        "session_id": r.session_id, "node_id": r.node_id, "requested_by": r.requested_by,
        "resolution": r.resolution, "fps_max": r.fps_max, "ttl_s": r.ttl_s,
        "stream_state": r.stream_state, "end_reason": r.end_reason, "started_at": r.started_at,
        "expires_at": r.expires_at, "ended_at": r.ended_at, "publisher_connected": False,
        "viewers": 0, "fps": 0.0, "frames": r.frames, "bytes": r.bytes, "dropped": 0,
    }


def _urls(request: Request, settings: Settings, session_id: str) -> tuple[str, str]:
    path = f"{settings.api_prefix}/stream/{session_id}"
    # El espectador llega por el mismo host por el que pidió la sesión
    base = str(request.base_url).rstrip("/").replace("http", "ws", 1)
    view = f"{base}{path}/view"
    # El nodo no necesariamente ve la API por ese host: URL pública si está configurada; si no,
    # ruta relativa que el nodo resuelve contra su CONFIG_MG_API_BASE_URL
    pub_base = settings.api_public_url.rstrip("/").replace("http", "ws", 1)
    return view, f"{pub_base}{path}/publish"


# ---------------------------------------------------------------- HTTP

@router.post("/sessions", status_code=201, response_model=StreamCreated,
             responses={200: {"description": "Ya había una sesión de ese nodo: se comparte"}})
async def create_session(
    body: StreamCreate, user: UserDep, db: DbDep, hub: HubDep, request: Request,
    response: Response, settings: SettingsDep,
) -> dict[str, Any]:
    node = await db.get(Node, body.node_id)
    if node is None or node.kind != "cam":
        raise ApiError(404, f"No existe la cámara {body.node_id}")

    s = hub.live_for(body.node_id)
    if s is not None:  # la cámara transmite una sola sesión: se suma a la que ya hay
        response.status_code = 200
    else:
        if node.node_state in (None, "offline"):
            raise ApiError(409, f"{body.node_id} no está en línea", code="node_offline")
        if hub.live_count() >= settings.stream_max_sessions:
            raise ApiError(409, f"Ya hay {settings.stream_max_sessions} sesiones activas",
                           code="stream_limit")
        cfg = (await config_svc.node_view(db, body.node_id))["config"]
        res = body.resolution
        if RES_ORDER[res] > RES_ORDER[cfg["stream_resolution_max"]]:
            res = cfg["stream_resolution_max"]
        fps = min(body.fps_max or cfg["stream_fps_max"], cfg["stream_fps_max"])
        ttl = min(body.ttl_s or DEFAULT_TTL_S, cfg["stream_ttl_max_s"])
        s = await hub.create(body.node_id, user.name, res, fps, ttl)

        _, publish_url = _urls(request, settings, s.session_id)
        sent = hub.send_cmd is not None and await hub.send_cmd(
            body.node_id, "start_stream", f"st-{s.session_id}",
            {"session_id": s.session_id, "ws_publish_url": publish_url, "resolution": res,
             "fps_max": fps, "ttl_s": ttl})
        if not sent:
            await hub.end(s, "broker_unavailable", state="error", notify_node=False)
            raise ApiError(503, "No se pudo enviar start_stream: broker MQTT sin conexión")

    view_url, publish_url = _urls(request, settings, s.session_id)
    return {**s.stats(), "token": s.token, "ws_view_url": view_url,
            "ws_publish_url": publish_url}


@router.get("/sessions", response_model=StreamList)
async def list_sessions(
    _: ReaderDep, db: DbDep, hub: HubDep, node_id: str | None = None,
    live: Annotated[bool, Query(description="solo las que siguen abiertas")] = False,
    limit: Annotated[int, Query(ge=1, le=200)] = 50,
) -> dict[str, Any]:
    if live:
        items = [s.stats() for s in hub.sessions.values()
                 if s.live and (node_id is None or s.node_id == node_id)]
        return {"items": sorted(items, key=lambda i: i["started_at"], reverse=True)}
    q = select(StreamSession).order_by(StreamSession.started_at.desc()).limit(limit)
    if node_id:
        q = q.where(StreamSession.node_id == node_id)
    rows = (await db.execute(q)).scalars().all()
    return {"items": [hub.sessions[r.session_id].stats() if r.session_id in hub.sessions
                      else _row_view(r) for r in rows]}


@router.get("/sessions/{session_id}", response_model=StreamOut)
async def get_session_state(session_id: str, _: ReaderDep, db: DbDep, hub: HubDep) -> Any:
    if s := hub.get(session_id):
        return s.stats()
    row = await db.get(StreamSession, session_id)
    if row is None:
        raise ApiError(404, f"No existe la sesión {session_id}")
    return _row_view(row)


@router.delete("/sessions/{session_id}", status_code=204)
async def delete_session(session_id: str, user: UserDep, db: DbDep, hub: HubDep) -> Response:
    if s := hub.get(session_id):
        await hub.end(s, f"stopped_by:{user.name}")
    elif await db.get(StreamSession, session_id) is None:
        raise ApiError(404, f"No existe la sesión {session_id}")
    return Response(status_code=204)


# ---------------------------------------------------------------- WebSocket

def _ws_token(ws: WebSocket, query_token: str | None) -> str | None:
    scheme, _, value = ws.headers.get("authorization", "").partition(" ")
    if scheme.lower() == "bearer" and value.strip():
        return value.strip()
    return query_token


async def _refuse(ws: WebSocket, code: int, reason: str) -> None:
    """Se acepta y se cierra con un código propio: el cliente puede saber por qué."""
    await ws.accept()
    await ws.close(code=code, reason=reason)


@router.websocket("/{session_id}/publish")
async def ws_publish(
    ws: WebSocket, session_id: str, db: DbDep, token: str | None = None,
) -> None:
    hub: StreamHub = ws.app.state.hub
    t = _ws_token(ws, token)
    node_id = await node_for_token(db, t) if t else None
    await db.close()  # la conexión dura minutos: no retener una conexión a la base
    if node_id is None:
        return await _refuse(ws, WS_UNAUTHORIZED, "invalid node token")
    s = hub.get(session_id)
    if s is None or not s.live:
        return await _refuse(ws, WS_ENDED, "session not active")
    if s.node_id != node_id:
        return await _refuse(ws, WS_FORBIDDEN, "session belongs to another node")
    await ws.accept()
    await hub.run_publisher(s, ws)


@router.websocket("/{session_id}/view")
async def ws_view(
    ws: WebSocket, session_id: str, settings: SettingsDep, token: str | None = None,
) -> None:
    hub: StreamHub = ws.app.state.hub
    s = hub.get(session_id)
    if s is None or not s.live:
        return await _refuse(ws, WS_ENDED, "session not active")
    if not s.check_token(_ws_token(ws, token)):
        return await _refuse(ws, WS_UNAUTHORIZED, "invalid session token")
    if len(s.viewers) >= settings.stream_max_viewers:
        return await _refuse(ws, WS_LIMIT, "too many viewers")
    await ws.accept()
    await hub.run_viewer(s, ws)
    with contextlib.suppress(Exception):  # la sesión terminó: que el espectador sepa por qué
        await ws.close(code=1000 if s.state != "error" else 1011, reason=(s.end_reason or "")[:120])
