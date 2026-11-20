"""Almacenamiento de imágenes detrás de una interfaz: hoy disco local, mañana S3/R2.

Las rutas son relativas y con '/', p. ej. "cam-01/2026/11/17/c-000123.jpg"; la base de datos
guarda solo esa ruta.
"""

import asyncio
import os
import tempfile
from functools import lru_cache
from pathlib import Path
from typing import Protocol

from app.core.config import get_settings


class ImageStore(Protocol):
    async def save(self, path: str, data: bytes) -> None: ...
    async def read(self, path: str) -> bytes | None: ...
    async def exists(self, path: str) -> bool: ...
    async def delete(self, path: str) -> None: ...


class LocalImageStore:
    def __init__(self, base: str | Path):
        self.base = Path(base).resolve()

    def _abs(self, path: str) -> Path:
        p = (self.base / path).resolve()
        if not p.is_relative_to(self.base):
            raise ValueError(f"ruta fuera del almacén: {path}")
        return p

    def _save_sync(self, path: str, data: bytes) -> None:
        dest = self._abs(path)
        dest.parent.mkdir(parents=True, exist_ok=True)
        # escritura atómica: temporal en la misma carpeta + rename
        fd, tmp = tempfile.mkstemp(dir=dest.parent, prefix=".tmp-")
        try:
            with os.fdopen(fd, "wb") as f:
                f.write(data)
            os.replace(tmp, dest)
        except BaseException:
            Path(tmp).unlink(missing_ok=True)
            raise

    def _read_sync(self, path: str) -> bytes | None:
        try:
            return self._abs(path).read_bytes()
        except FileNotFoundError:
            return None

    async def save(self, path: str, data: bytes) -> None:
        await asyncio.to_thread(self._save_sync, path, data)

    async def read(self, path: str) -> bytes | None:
        return await asyncio.to_thread(self._read_sync, path)

    async def exists(self, path: str) -> bool:
        return await asyncio.to_thread(self._abs(path).is_file)

    async def delete(self, path: str) -> None:
        await asyncio.to_thread(self._abs(path).unlink, True)


@lru_cache
def get_image_store() -> ImageStore:
    return LocalImageStore(get_settings().images_dir)
