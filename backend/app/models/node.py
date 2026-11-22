"""Tabla nodes (Contrato §6). La llena el ingestor MQTT (B2) y el alta con token (B4)."""

from datetime import datetime
from typing import Any

from sqlalchemy import DateTime, ForeignKey, String, func
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base
from app.models.types import JsonType


class Node(Base):
    __tablename__ = "nodes"

    node_id: Mapped[str] = mapped_column(String(32), primary_key=True)
    kind: Mapped[str] = mapped_column(String(8), index=True)  # cam | ctrl | disp
    camera_type: Mapped[str | None] = mapped_column(String(32))
    firmware: Mapped[str | None] = mapped_column(String(32))
    zone_id: Mapped[str | None] = mapped_column(
        String(24), ForeignKey("zones.zone_id", ondelete="RESTRICT"), index=True
    )
    node_state: Mapped[str | None] = mapped_column(String(16))
    last_seen: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    status_ts: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    last_status: Mapped[dict[str, Any] | None] = mapped_column(JsonType)
    registered_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))  # POST /nodes
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())
