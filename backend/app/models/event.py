"""Tabla events (Contrato §6)."""

from datetime import datetime
from typing import Any

from sqlalchemy import DateTime, Index, String, UniqueConstraint, func
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base
from app.models.types import IdType, JsonType


class Event(Base):
    __tablename__ = "events"
    __table_args__ = (
        # QoS 1 puede entregar el mismo mensaje dos veces
        UniqueConstraint("node_id", "event_type", "started_at",
                         name="uq_events_node_id_event_type_started_at"),
        Index("ix_events_started_at_id", "started_at", "id"),
    )

    id: Mapped[int] = mapped_column(IdType, primary_key=True, autoincrement=True)
    node_id: Mapped[str] = mapped_column(String(32), index=True)
    event_type: Mapped[str] = mapped_column(String(32))
    severity: Mapped[str] = mapped_column(String(8))
    event_state: Mapped[str] = mapped_column(String(16), default="open", index=True)
    started_at: Mapped[datetime] = mapped_column(DateTime(timezone=True))
    acknowledged_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    acknowledged_by: Mapped[str | None] = mapped_column(String(64))
    resolved_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    capture_id: Mapped[str | None] = mapped_column(String(32))
    detail: Mapped[Any] = mapped_column(JsonType, nullable=True)
    received_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    @property
    def event_id(self) -> str:
        return format_event_id(self.id)


def format_event_id(n: int) -> str:
    return f"e-{n:06d}"


def parse_event_id(event_id: str) -> int | None:
    s = event_id.removeprefix("e-")
    return int(s) if s.isdigit() else None
