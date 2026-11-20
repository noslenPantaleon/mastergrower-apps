"""Ingestor MQTT (B2): se suscribe a estado y eventos de todos los nodos y los guarda en la base.

Corre como tarea de fondo dentro del proceso de la API (arranca en el lifespan). Usa sesión
persistente (clean_session=False, identificador fijo): si la API se cae, Mosquitto le guarda los
eventos QoS 1 y se los entrega al volver. Con varios workers de uvicorn habría que sacarlo a un
proceso aparte (dos clientes con el mismo identificador se patean entre sí).
"""

import asyncio
import json
import logging
import random
from datetime import UTC, datetime

import aiomqtt

from app.core.config import Settings
from app.core.db import get_sessionmaker
from app.services.ingest import handle_message

log = logging.getLogger(__name__)

TOPICS = ["growlab/+/+/status", "growlab/+/+/state", "growlab/+/+/event"]


class MqttIngestor:
    def __init__(self, settings: Settings):
        self._s = settings
        self.connected = False
        self.last_message_at: datetime | None = None
        self.messages = 0
        self._task: asyncio.Task | None = None
        self._client: aiomqtt.Client | None = None

    def start(self) -> None:
        self._task = asyncio.create_task(self._run(), name="mqtt-ingestor")

    async def stop(self) -> None:
        if self._task:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass

    async def _run(self) -> None:
        backoff = 1.0
        while True:
            try:
                async with aiomqtt.Client(
                    self._s.mqtt_host, self._s.mqtt_port,
                    username=self._s.mqtt_api_user, password=self._s.mqtt_api_password or None,
                    identifier=self._s.mqtt_client_id, clean_session=False, keepalive=30,
                ) as client:
                    self._client = client
                    for t in TOPICS:
                        await client.subscribe(t, qos=1)
                    self.connected, backoff = True, 1.0
                    log.info("Ingestor MQTT conectado a %s:%s",
                             self._s.mqtt_host, self._s.mqtt_port)
                    async for msg in client.messages:
                        await self._handle(msg)
            except aiomqtt.MqttError as e:
                was = self.connected
                self.connected, self._client = False, None
                wait = backoff * random.uniform(0.8, 1.2)
                (log.warning if was else log.info)(
                    "Ingestor MQTT sin conexión (%s); reintento en %.0f s", e, wait)
                await asyncio.sleep(wait)
                backoff = min(backoff * 2, 60)

    async def _handle(self, msg: aiomqtt.Message) -> None:
        self.messages += 1
        self.last_message_at = datetime.now(UTC)
        p = msg.payload
        payload = bytes(p) if isinstance(p, bytes | bytearray) else str(p).encode()
        try:
            async with get_sessionmaker()() as db:
                await handle_message(db, msg.topic.value, payload, retained=bool(msg.retain))
        except Exception:
            # Un mensaje que rompe algo (p. ej. base caída) no debe matar al ingestor
            log.exception("Ingestor MQTT: error procesando %s", msg.topic.value)

    async def publish(self, topic: str, message: dict, qos: int = 1) -> bool:
        """Publica con el mismo cliente. Mejor esfuerzo: False si no hay conexión."""
        client = self._client
        if client is None:
            return False
        try:
            await client.publish(topic, json.dumps(message, separators=(",", ":")), qos=qos)
            return True
        except aiomqtt.MqttError as e:
            log.warning("No se pudo publicar en %s: %s", topic, e)
            return False


def command(node_id: str, action: str, cmd_id: str, params: dict | None = None) -> dict:
    """Mensaje cmd según contract/mqtt/schemas/cmd.schema.json (emisor: la API)."""
    msg = {"schema_version": 1, "node_id": "api",
           "ts": datetime.now(UTC).strftime("%Y-%m-%dT%H:%M:%SZ"),
           "action": action, "cmd_id": cmd_id}
    if params is not None:
        msg["params"] = params
    return msg
