"""Reparto de cuadros de la vista en vivo (B5, Contrato §5.6).

Una sesión tiene un publicador (el nodo, por WebSocket) y hasta N espectadores. Cada espectador
tiene un único casillero con el último cuadro: si el envío anterior todavía no terminó (red lenta),
el cuadro nuevo pisa al pendiente y se cuenta como descartado. Así un espectador lento nunca
acumula atraso ni frena al resto.

Vive en memoria del proceso de la API (igual que el ingestor MQTT): con varios workers habría que
llevarlo a un proceso aparte o a un broker de cuadros.
"""

import asyncio
import contextlib
import hashlib
import hmac
import logging
import secrets
import time
from collections import deque
from collections.abc import Awaitable, Callable
from dataclasses import dataclass, field
from datetime import UTC, datetime, timedelta
from typing import Any

from fastapi import WebSocket
from sqlalchemy import update
from sqlalchemy.ext.asyncio import AsyncSession, async_sessionmaker

from app.core.config import Settings
from app.models.stream import StreamSession

log = logging.getLogger(__name__)

FPS_WINDOW_S = 5.0
JPEG_SOI = b"\xff\xd8"

# Códigos de cierre de WebSocket (4000–4999: de la aplicación)
WS_UNAUTHORIZED = 4401
WS_FORBIDDEN = 4403
WS_NOT_FOUND = 4404
WS_LIMIT = 4409
WS_ENDED = 4410

CmdSender = Callable[[str, str, str, dict[str, Any]], Awaitable[bool]]


def hash_token(token: str) -> str:
    return hashlib.sha256(token.encode()).hexdigest()


@dataclass(eq=False)
class Viewer:
    ws: WebSocket
    frame: bytes | None = None
    ready: asyncio.Event = field(default_factory=asyncio.Event)
    sent: int = 0
    dropped: int = 0

    def push(self, frame: bytes) -> None:
        if self.frame is not None:
            self.dropped += 1  # el anterior no llegó a salir: se descarta
        self.frame = frame
        self.ready.set()


@dataclass(eq=False)
class LiveSession:
    session_id: str
    node_id: str
    requested_by: str
    resolution: str
    fps_max: int
    ttl_s: int
    token: str
    started_at: datetime
    expires_at: datetime
    state: str = "requested"
    end_reason: str | None = None
    ended_at: datetime | None = None
    publisher: WebSocket | None = None
    viewers: set[Viewer] = field(default_factory=set)
    last_frame: bytes | None = None
    frames: int = 0
    bytes: int = 0
    dropped_closed: int = 0  # descartes de espectadores que ya se fueron
    created_mono: float = field(default_factory=time.monotonic)
    idle_since: float | None = field(default_factory=time.monotonic)
    frame_times: deque[float] = field(default_factory=lambda: deque(maxlen=200))
    first_frame_mono: float | None = None
    ended: asyncio.Event = field(default_factory=asyncio.Event)

    @property
    def live(self) -> bool:
        return self.state in ("requested", "active")

    @property
    def fps(self) -> float:
        """Cuadros recibidos en los últimos FPS_WINDOW_S segundos / duración de la ventana.
        (No se usa el intervalo entre el primero y el último: por TCP llegan en ráfagas.)"""
        now = time.monotonic()
        recent = [t for t in self.frame_times if now - t <= FPS_WINDOW_S]
        if len(recent) < 2 or self.first_frame_mono is None:
            return 0.0
        window = min(FPS_WINDOW_S, now - self.first_frame_mono)
        return round(len(recent) / max(window, 1e-3), 1)

    @property
    def dropped(self) -> int:
        return self.dropped_closed + sum(v.dropped for v in self.viewers)

    def check_token(self, token: str | None) -> bool:
        return bool(token) and hmac.compare_digest(hash_token(token), hash_token(self.token))

    def stats(self) -> dict[str, Any]:
        return {
            "session_id": self.session_id, "node_id": self.node_id,
            "requested_by": self.requested_by, "resolution": self.resolution,
            "fps_max": self.fps_max, "ttl_s": self.ttl_s, "stream_state": self.state,
            "end_reason": self.end_reason, "started_at": self.started_at,
            "expires_at": self.expires_at, "ended_at": self.ended_at,
            "publisher_connected": self.publisher is not None, "viewers": len(self.viewers),
            "fps": self.fps, "frames": self.frames, "bytes": self.bytes, "dropped": self.dropped,
        }


class StreamHub:
    def __init__(self, settings: Settings):
        self.s = settings
        self.sessions: dict[str, LiveSession] = {}
        # Se inyectan al arrancar (lifespan) o en los tests
        self.sessionmaker: Callable[[], async_sessionmaker[AsyncSession]] | None = None
        self.send_cmd: CmdSender | None = None
        self._reaper: asyncio.Task | None = None

    # ------------------------------------------------------------------ ciclo de vida

    def start(self) -> None:
        self._reaper = asyncio.create_task(self._reap_loop(), name="stream-reaper")

    async def stop(self) -> None:
        if self._reaper:
            self._reaper.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._reaper
        for s in list(self.sessions.values()):
            await self.end(s, "api_shutdown", notify_node=True)

    async def close_stale(self) -> int:
        """Al arrancar: las sesiones que quedaron abiertas en la base murieron con el proceso."""
        async with self._db() as db:
            r = await db.execute(
                update(StreamSession)
                .where(StreamSession.stream_state.in_(("requested", "active")))
                .values(stream_state="ended", end_reason="api_restart",
                        ended_at=datetime.now(UTC)))
            await db.commit()
            return r.rowcount or 0

    def _db(self) -> AsyncSession:
        assert self.sessionmaker is not None
        return self.sessionmaker()()

    # ------------------------------------------------------------------ sesiones

    def live_for(self, node_id: str) -> LiveSession | None:
        return next((s for s in self.sessions.values() if s.node_id == node_id and s.live), None)

    def live_count(self) -> int:
        return sum(1 for s in self.sessions.values() if s.live)

    def get(self, session_id: str) -> LiveSession | None:
        return self.sessions.get(session_id)

    async def create(
        self, node_id: str, requested_by: str, resolution: str, fps_max: int, ttl_s: int
    ) -> LiveSession:
        now = datetime.now(UTC).replace(microsecond=0)
        s = LiveSession(
            session_id="s-" + secrets.token_hex(6), node_id=node_id, requested_by=requested_by,
            resolution=resolution, fps_max=fps_max, ttl_s=ttl_s,
            token=secrets.token_urlsafe(24), started_at=now,
            expires_at=now + timedelta(seconds=ttl_s),
        )
        async with self._db() as db:
            db.add(StreamSession(
                session_id=s.session_id, node_id=node_id, requested_by=requested_by,
                resolution=resolution, fps_max=fps_max, ttl_s=ttl_s, stream_state=s.state,
                token_hash=hash_token(s.token), started_at=s.started_at,
                expires_at=s.expires_at))
            await db.commit()
        self.sessions[s.session_id] = s
        log.info("Sesión %s para %s (%s, %d fps, %d s) pedida por %s", s.session_id, node_id,
                 resolution, fps_max, ttl_s, requested_by)
        return s

    async def _persist(self, s: LiveSession) -> None:
        try:
            async with self._db() as db:
                await db.execute(
                    update(StreamSession).where(StreamSession.session_id == s.session_id).values(
                        stream_state=s.state, end_reason=s.end_reason, ended_at=s.ended_at,
                        frames=s.frames, bytes=s.bytes))
                await db.commit()
        except Exception:
            log.exception("No se pudo guardar el estado de la sesión %s", s.session_id)

    async def end(
        self, s: LiveSession, reason: str, *, state: str = "ended", notify_node: bool = True
    ) -> None:
        """Termina la sesión: avisa al nodo (stop_stream), cierra los WebSocket y la guarda."""
        if not s.live:
            return
        s.state, s.end_reason = state, reason[:40]
        s.ended_at = datetime.now(UTC).replace(microsecond=0)
        s.ended.set()
        log.info("Sesión %s de %s terminada: %s (%d cuadros, %.0f KB, %d descartados)",
                 s.session_id, s.node_id, reason, s.frames, s.bytes / 1024, s.dropped)
        if notify_node and self.send_cmd:
            await self.send_cmd(s.node_id, "stop_stream", f"sp-{s.session_id}",
                                {"session_id": s.session_id})
        code = 1000 if state == "ended" else 1011
        sockets = [v.ws for v in s.viewers] + ([s.publisher] if s.publisher else [])
        for ws in sockets:
            with contextlib.suppress(Exception):
                await ws.close(code=code, reason=reason[:120])
        await self._persist(s)
        self.sessions.pop(s.session_id, None)

    async def handle_ack(self, node_id: str, ack: dict[str, Any]) -> None:
        """cmd/ack de un nodo: un start_stream rechazado termina la sesión con error."""
        cmd_id = str(ack.get("cmd_id", ""))
        if not cmd_id.startswith("st-"):
            return
        s = self.sessions.get(cmd_id[3:])
        if s is None or s.node_id != node_id:
            return
        if ack.get("result") == "rejected":
            await self.end(s, f"rejected:{ack.get('reason') or 'unknown'}", state="error",
                           notify_node=False)

    # ------------------------------------------------------------------ cuadros

    def on_frame(self, s: LiveSession, frame: bytes) -> None:
        s.frames += 1
        s.bytes += len(frame)
        s.last_frame = frame
        now = time.monotonic()
        if s.first_frame_mono is None:
            s.first_frame_mono = now
        s.frame_times.append(now)
        for v in s.viewers:
            v.push(frame)

    async def run_publisher(self, s: LiveSession, ws: WebSocket) -> None:
        """Recibe los cuadros del nodo hasta que cierre o termine la sesión."""
        if s.publisher is not None:  # el nodo reconectó: el WebSocket viejo sobra
            with contextlib.suppress(Exception):
                await s.publisher.close(code=WS_LIMIT, reason="replaced")
        s.publisher = ws
        if s.state == "requested":
            s.state = "active"
            await self._persist(s)
            log.info("Sesión %s: %s empezó a publicar (%.1f s desde el pedido)", s.session_id,
                     s.node_id, time.monotonic() - s.created_mono)
        bad = 0
        try:
            while s.live:
                msg = await ws.receive()
                if msg["type"] == "websocket.disconnect":
                    break
                data = msg.get("bytes")
                if data is None:
                    continue  # texto opcional (ts y número de cuadro): no hace falta reenviarlo
                if len(data) > self.s.stream_max_frame_bytes or not data.startswith(JPEG_SOI):
                    bad += 1
                    if bad in (1, 100):
                        log.warning("Sesión %s: cuadro inválido (%d bytes)", s.session_id,
                                    len(data))
                    continue
                self.on_frame(s, data)
        finally:
            if s.publisher is ws:
                s.publisher = None
                if s.live:
                    await self.end(s, "publisher_closed", notify_node=False)

    async def run_viewer(self, s: LiveSession, ws: WebSocket) -> None:
        v = Viewer(ws)
        s.viewers.add(v)
        s.idle_since = None
        if s.last_frame is not None:
            v.push(s.last_frame)  # que vea algo enseguida
        sender = asyncio.create_task(self._send_loop(s, v))
        receiver = asyncio.create_task(self._drain(ws))
        ended = asyncio.create_task(s.ended.wait())
        try:
            await asyncio.wait({sender, receiver, ended}, return_when=asyncio.FIRST_COMPLETED)
        finally:
            for t in (sender, receiver, ended):
                t.cancel()
            s.viewers.discard(v)
            s.dropped_closed += v.dropped
            if not s.viewers:
                s.idle_since = time.monotonic()

    @staticmethod
    async def _send_loop(s: LiveSession, v: Viewer) -> None:
        while s.live:
            await v.ready.wait()
            v.ready.clear()
            frame, v.frame = v.frame, None
            if frame is not None:
                await v.ws.send_bytes(frame)
                v.sent += 1

    @staticmethod
    async def _drain(ws: WebSocket) -> None:
        """El espectador no manda nada útil; solo interesa enterarse de que se fue."""
        while True:
            msg = await ws.receive()
            if msg["type"] == "websocket.disconnect":
                return

    # ------------------------------------------------------------------ vencimientos

    async def reap(self) -> None:
        now_mono, now = time.monotonic(), datetime.now(UTC)
        start_timeout = self.s.stream_start_timeout_s
        for s in list(self.sessions.values()):
            if not s.live:
                continue
            if now >= s.expires_at:
                await self.end(s, "ttl")
            elif s.state == "requested" and now_mono - s.created_mono > start_timeout:
                await self.end(s, "node_timeout", state="error")
            elif s.idle_since is not None and now_mono - s.idle_since > self.s.stream_idle_s:
                await self.end(s, "no_viewers")

    async def _reap_loop(self) -> None:
        while True:
            await asyncio.sleep(1)
            try:
                await self.reap()
            except Exception:
                log.exception("Error revisando las sesiones de stream")
