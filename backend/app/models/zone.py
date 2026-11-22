"""Tabla zones (Contrato §5.5 y §6): zonas de cultivo a las que pertenece cada nodo."""

from datetime import datetime
from typing import Any

from sqlalchemy import DateTime, String, func
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base
from app.models.types import JsonType


class Zone(Base):
    __tablename__ = "zones"

    zone_id: Mapped[str] = mapped_column(String(24), primary_key=True)
    name: Mapped[str] = mapped_column(String(64))
    grow_mode: Mapped[str | None] = mapped_column(String(16))  # substrate | coco_coir | dwc
    # Ventana de luz de la zona {start, end}: nivel de config entre la global y el override
    light_cycle: Mapped[dict[str, Any] | None] = mapped_column(JsonType)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )
