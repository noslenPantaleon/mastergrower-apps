"""Cursor opaco para listados ordenados por (fecha desc, id desc): "<µs desde epoch>_<id>"."""

from datetime import UTC, datetime

from app.core.errors import ApiError


def aware(dt: datetime) -> datetime:
    """SQLite devuelve fechas sin zona: se asumen UTC."""
    return dt if dt.tzinfo else dt.replace(tzinfo=UTC)


def encode_cursor(ts: datetime, row_id: int) -> str:
    return f"{int(aware(ts).timestamp() * 1_000_000)}_{row_id}"


def decode_cursor(cursor: str) -> tuple[datetime, int]:
    try:
        us, _, rid = cursor.partition("_")
        return datetime.fromtimestamp(int(us) / 1_000_000, UTC), int(rid)
    except ValueError as e:
        raise ApiError(400, "Cursor inválido") from e
