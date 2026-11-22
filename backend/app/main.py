"""Punto de entrada de la API MasterGrow."""

import logging
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager

from fastapi import FastAPI

from app import __version__
from app.api.v1.router import api_router
from app.core.config import get_settings
from app.core.db import dispose_engine, get_sessionmaker
from app.core.errors import install_handlers
from app.mqtt.ingestor import MqttIngestor
from app.services.nodes import import_env_tokens
from app.stream.hub import StreamHub
from app.web import gallery, live

logging.basicConfig(level=logging.INFO, format="%(levelname)s %(name)s: %(message)s")
log = logging.getLogger("mastergrow")


@asynccontextmanager
async def lifespan(app: FastAPI) -> AsyncIterator[None]:
    s = get_settings()
    if s.node_tokens:
        try:
            async with get_sessionmaker()() as db:
                await import_env_tokens(db, s.node_tokens)
        except Exception as e:  # la base caída no impide arrancar: /health lo informa
            log.error("No se pudieron importar API_NODE_TOKENS: %s", e)
    ingestor = MqttIngestor(s) if s.mqtt_ingest_enabled else None
    app.state.ingestor = ingestor
    hub: StreamHub = app.state.hub
    hub.sessionmaker = get_sessionmaker
    try:
        if n := await hub.close_stale():
            log.info("Sesiones de stream que quedaron abiertas: %d (cerradas)", n)
    except Exception as e:
        log.error("No se pudieron cerrar las sesiones viejas: %s", e)
    if ingestor:
        hub.send_cmd = ingestor.send_command
        ingestor.on_ack = hub.handle_ack
        ingestor.start()
    hub.start()
    yield
    await hub.stop()
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
    app.state.hub = StreamHub(settings)  # vista en vivo (B5); se completa en el lifespan
    install_handlers(app)
    app.include_router(api_router, prefix=settings.api_prefix)
    app.include_router(gallery.router)
    app.include_router(live.router)
    return app


app = create_app()
