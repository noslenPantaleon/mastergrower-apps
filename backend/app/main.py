"""Punto de entrada de la API MasterGrow."""

from collections.abc import AsyncIterator
from contextlib import asynccontextmanager

from fastapi import FastAPI

from app import __version__
from app.api.v1.router import api_router
from app.core.config import get_settings
from app.core.db import dispose_engine


@asynccontextmanager
async def lifespan(_: FastAPI) -> AsyncIterator[None]:
    # B2: acá se arranca el ingestor MQTT (estado y eventos de los nodos).
    yield
    await dispose_engine()


def create_app() -> FastAPI:
    settings = get_settings()
    app = FastAPI(
        title="MasterGrow API",
        version=__version__,
        description="API del sistema de cultivo indoor (ver contract/ en el repositorio).",
        lifespan=lifespan,
    )
    app.include_router(api_router, prefix=settings.api_prefix)
    return app


app = create_app()
