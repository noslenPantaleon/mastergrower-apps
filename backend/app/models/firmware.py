"""Tabla firmware: imágenes para actualizar los nodos por OTA (plan C9/D6)."""

from datetime import datetime

from sqlalchemy import DateTime, Integer, String, UniqueConstraint, func
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base
from app.models.types import IdType


class Firmware(Base):
    __tablename__ = "firmware"
    __table_args__ = (UniqueConstraint("kind", "version"),)

    id: Mapped[int] = mapped_column(IdType, primary_key=True, autoincrement=True)
    kind: Mapped[str] = mapped_column(String(8), index=True)      # cam | disp | ctrl
    version: Mapped[str] = mapped_column(String(32))               # esp_app_desc_t.version
    project: Mapped[str] = mapped_column(String(32))               # esp_app_desc_t.project_name
    idf_ver: Mapped[str] = mapped_column(String(32))
    size: Mapped[int] = mapped_column(Integer)
    sha256: Mapped[str] = mapped_column(String(64))
    path: Mapped[str] = mapped_column(String(128))                 # dentro del almacén de firmware
    uploaded_by: Mapped[str] = mapped_column(String(64))
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())


def format_firmware_id(n: int) -> str:
    return f"f-{n:06d}"


def parse_firmware_id(s: str) -> int | None:
    if not s.startswith("f-") or not s[2:].isdigit():
        return None
    return int(s[2:])
