"""Fuente ejecutable del contrato (Contrato §8.3): rutas de contract/ y exportación de OpenAPI."""

import json
from pathlib import Path
from typing import Any

CONTRACT_DIR = Path(__file__).resolve().parents[2] / "contract"
OPENAPI_PATH = CONTRACT_DIR / "openapi.json"
MQTT_SCHEMAS = CONTRACT_DIR / "mqtt" / "schemas"
HTTP_SCHEMAS = CONTRACT_DIR / "http" / "schemas"

# Canal MQTT → esquema (contract/mqtt/topics.md)
CHANNEL_SCHEMA = {
    ("cam", "status"): "cam_status.schema.json",
    ("ctrl", "state"): "ctrl_state.schema.json",
    ("cam", "event"): "cam_event.schema.json",
    ("cam", "cmd"): "cmd.schema.json",
    ("ctrl", "cmd"): "cmd.schema.json",
    ("cam", "cmd/ack"): "cmd_ack.schema.json",
    ("ctrl", "cmd/ack"): "cmd_ack.schema.json",
}


def load_schema(name: str) -> dict[str, Any]:
    for d in (MQTT_SCHEMAS, HTTP_SCHEMAS):
        if (d / name).exists():
            return json.loads((d / name).read_text())
    raise FileNotFoundError(name)


def openapi_document() -> dict[str, Any]:
    from app.main import create_app

    return create_app().openapi()


def openapi_text() -> str:
    """Serialización estable: el archivo solo cambia si cambia la API."""
    return json.dumps(openapi_document(), indent=2, ensure_ascii=False, sort_keys=True) + "\n"
