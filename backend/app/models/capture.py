"""Tabla captures (Contrato §6)."""

from datetime import datetime
from typing import Any

from sqlalchemy import DateTime, String, UniqueConstraint, func
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base
from app.models.types import IdType, JsonType


class Capture(Base):
    __tablename__ = "captures"
    __table_args__ = (UniqueConstraint("node_id", "local_id", name="uq_captures_node_id_local_id"),)

    id: Mapped[int] = mapped_column(IdType, primary_key=True, autoincrement=True)
    node_id: Mapped[str] = mapped_column(String(32), index=True)
    timestamp: Mapped[datetime] = mapped_column(DateTime(timezone=True), index=True)
    timestamp_approx: Mapped[bool] = mapped_column(default=False)
    received_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    camera_type: Mapped[str] = mapped_column(String(32))
    trigger_type: Mapped[str] = mapped_column(String(32), index=True)
    resolution: Mapped[str] = mapped_column(String(8))
    width: Mapped[int | None]
    height: Mapped[int | None]
    priority: Mapped[str] = mapped_column(String(8))
    seq: Mapped[int | None]
    sensor_snapshot: Mapped[dict[str, Any] | None] = mapped_column(JsonType)
    local_analysis: Mapped[dict[str, Any] | None] = mapped_column(JsonType)
    server_analysis: Mapped[dict[str, Any] | None] = mapped_column(JsonType)
    upload_state: Mapped[str] = mapped_column(String(16), default="received")
    image_path: Mapped[str] = mapped_column(String(255))
    image_bytes: Mapped[int]
    local_id: Mapped[str | None] = mapped_column(String(64))  # Idempotency-Key del nodo

    @property
    def capture_id(self) -> str:
        return format_id(self.id)


def format_id(n: int) -> str:
    return f"c-{n:06d}"


def parse_id(capture_id: str) -> int | None:
    """"c-000123" o "123" → 123."""
    s = capture_id.removeprefix("c-")
    return int(s) if s.isdigit() else None
