"""Tipos de columna que se comportan igual en Postgres y en SQLite (tests)."""

from sqlalchemy import JSON, BigInteger, Integer
from sqlalchemy.dialects.postgresql import JSONB

# JSONB en Postgres, JSON genérico en SQLite
JsonType = JSON().with_variant(JSONB(), "postgresql")
# BIGINT autoincremental en Postgres; SQLite solo autoincrementa INTEGER PRIMARY KEY
IdType = BigInteger().with_variant(Integer(), "sqlite")
