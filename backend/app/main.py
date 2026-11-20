"""Punto de entrada de la API MasterGrow."""

import logging
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager

from fastapi import FastAPI

from app import __version__
from app.api.v1.router import api_router
from app.core.config import get_settings
from app.core.db import dispose_engine
from app.core.errors import install_handlers
from app.mqtt.ingestor import MqttIngestor
from app.web import gallery

logging.basicConfig(level=logging.INFO, format="%(levelname)s %(name)s: %(message)s")
log = logging.getLogger("mastergrow")


@asynccontextmanager
async def lifespan(app: FastAPI) -> AsyncIterator[None]:
    s = get_settings()
    if not s.node_tokens:
        log.warning("API_NODE_TOKENS vacío: ningún nodo podrá subir capturas")
    ingestor = MqttIngestor(s) if s.mqtt_ingest_enabled else None
    app.state.ingestor = ingestor
    if ingestor:
        ingestor.start()
    yield
    if ingestor:
        await ingestor.stop()
    await dispose_engine()


def create_app() -> FastAPI:
    settings = get_settings()
    app = FastAPI(
        title="MasterGrow API",
        version=__version__,
        description="API del sistema de cultivo indoor (ver contract/ en el repositorio).",
        lifespan=lifespan,
    )
    install_handlers(app)
    app.include_router(api_router, prefix=settings.api_prefix)
    app.include_router(gallery.router)
    return app


app = create_app()
