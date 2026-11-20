"""Tablas config_global, config_node y config_revisions (Contrato §6 y §7)."""

from datetime import datetime
from typing import Any

from sqlalchemy import DateTime, String, func
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base
from app.models.types import IdType, JsonType

GLOBAL_SCOPE = "global"


class ConfigGlobal(Base):
    """Valores globales que pisan al por defecto (solo los que se cambiaron)."""

    __tablename__ = "config_global"

    key: Mapped[str] = mapped_column(String(64), primary_key=True)
    value: Mapped[Any] = mapped_column(JsonType)
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )


class ConfigNode(Base):
    """Override de un nodo: solo las excepciones a la global."""

    __tablename__ = "config_node"

    node_id: Mapped[str] = mapped_column(String(32), primary_key=True)
    key: Mapped[str] = mapped_column(String(64), primary_key=True)
    value: Mapped[Any] = mapped_column(JsonType)
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )


class ConfigRevision(Base):
    """Cada cambio de configuración. Su id es la versión (entero que solo crece)."""

    __tablename__ = "config_revisions"

    id: Mapped[int] = mapped_column(IdType, primary_key=True, autoincrement=True)
    scope: Mapped[str] = mapped_column(String(32), index=True)  # "global" o un node_id
    changed_by: Mapped[str] = mapped_column(String(64))
    changes: Mapped[dict[str, Any]] = mapped_column(JsonType)  # {clave: valor nuevo | None}
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
