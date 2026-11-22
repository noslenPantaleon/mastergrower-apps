"""Tabla stream_sessions (Contrato §5.6 y §6): historial de sesiones de vista en vivo.

El reparto de cuadros vive en memoria (app/stream/hub.py); la tabla guarda quién pidió cada
sesión, cómo terminó y cuánto transmitió.
"""

from datetime import datetime

from sqlalchemy import BigInteger, DateTime, Integer, String, func
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base


class StreamSession(Base):
    __tablename__ = "stream_sessions"

    session_id: Mapped[str] = mapped_column(String(16), primary_key=True)
    node_id: Mapped[str] = mapped_column(String(32), index=True)
    requested_by: Mapped[str] = mapped_column(String(64))
    resolution: Mapped[str] = mapped_column(String(8))  # qvga | vga
    fps_max: Mapped[int] = mapped_column(Integer)
    ttl_s: Mapped[int] = mapped_column(Integer)
    # requested | active | ended | error
    stream_state: Mapped[str] = mapped_column(String(12), index=True)
    end_reason: Mapped[str | None] = mapped_column(String(40))
    token_hash: Mapped[str] = mapped_column(String(64))
    frames: Mapped[int] = mapped_column(Integer, default=0)
    bytes: Mapped[int] = mapped_column(BigInteger().with_variant(Integer(), "sqlite"), default=0)
    started_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())
    expires_at: Mapped[datetime] = mapped_column(DateTime(timezone=True))
    ended_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
