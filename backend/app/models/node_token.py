"""Tabla node_tokens (Contrato §8.2): token por nodo, emitido al darlo de alta y revocable.

Solo se guarda el SHA-256 del token: el valor en claro se muestra una única vez al emitirlo.
"""

from datetime import datetime

from sqlalchemy import DateTime, String, func
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base
from app.models.types import IdType


class NodeToken(Base):
    __tablename__ = "node_tokens"

    id: Mapped[int] = mapped_column(IdType, primary_key=True, autoincrement=True)
    node_id: Mapped[str] = mapped_column(String(32), index=True)
    token_hash: Mapped[str] = mapped_column(String(64), unique=True)
    source: Mapped[str] = mapped_column(String(8))  # api (POST /nodes) | env (API_NODE_TOKENS)
    created_by: Mapped[str] = mapped_column(String(64))
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())
    revoked_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
