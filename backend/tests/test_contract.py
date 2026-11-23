"""Tests de contrato (B6): la API y contract/ no se desalinean (Contrato §8.3)."""

import json
from typing import get_args

import pytest
from jsonschema import Draft202012Validator, FormatChecker

from app.contract import (
    CONTRACT_DIR,
    HTTP_SCHEMAS,
    MQTT_SCHEMAS,
    OPENAPI_PATH,
    load_schema,
    openapi_text,
)
from app.mqtt.ingestor import command
from app.schemas.common import CameraType, EventType, NodeState, Severity
from app.schemas.mqtt import EventMsg, StatusMsg
from app.schemas.stream import StreamResolution
from tests.conftest import USER

ALL_SCHEMAS = sorted([*MQTT_SCHEMAS.glob("*.json"), *HTTP_SCHEMAS.glob("*.json")])


def validator(name: str) -> Draft202012Validator:
    return Draft202012Validator(load_schema(name), format_checker=FormatChecker())


def errors(name: str, data) -> list[str]:
    return [f"{'.'.join(map(str, e.path))}: {e.message}" for e in validator(name).iter_errors(data)]


def enum_of(schema_name: str, prop: str) -> set:
    return set(load_schema(schema_name)["properties"][prop]["enum"])


# ---------------------------------------------------------------- OpenAPI

def test_openapi_up_to_date():
    assert OPENAPI_PATH.exists(), "falta contract/openapi.json"
    assert OPENAPI_PATH.read_text() == openapi_text(), (
        "contract/openapi.json desactualizado: correr `uv run python scripts/export_contract.py` "
        "y anotar el cambio en contract/CHANGELOG.md")


def test_openapi_has_contract_routes():
    paths = json.loads(OPENAPI_PATH.read_text())["paths"]
    for p in ["/api/v1/captures", "/api/v1/events", "/api/v1/nodes", "/api/v1/zones",
              "/api/v1/config/global", "/api/v1/config/nodes/{node_id}",
              "/api/v1/stream/sessions", "/api/v1/stream/sessions/{session_id}"]:
        assert p in paths, p


# ---------------------------------------------------------------- JSON Schema

@pytest.mark.parametrize("path", ALL_SCHEMAS, ids=lambda p: p.name)
def test_schema_is_valid_and_examples_pass(path):
    schema = json.loads(path.read_text())
    Draft202012Validator.check_schema(schema)
    v = Draft202012Validator(schema, format_checker=FormatChecker())
    for ex in schema.get("examples", []):
        assert not list(v.iter_errors(ex)), f"{path.name}: el ejemplo no cumple su esquema"


def test_every_schema_is_documented():
    """Cada esquema aparece citado en algún .md del contrato."""
    docs = "".join(p.read_text() for p in CONTRACT_DIR.rglob("*.md"))
    for path in ALL_SCHEMAS:
        assert path.name in docs, f"{path.name} no está citado en contract/"


# ---------------------------------------------------------------- MQTT: lo que publica la API

@pytest.mark.parametrize("action,params", [
    ("capture_now", None),
    ("reboot", None),
    ("refresh_config", {"version": 7}),
    ("start_stream", {"session_id": "s-abc", "ws_publish_url": "/api/v1/stream/s-abc/publish",
                      "resolution": "qvga", "fps_max": 10, "ttl_s": 120}),
    ("stop_stream", {"session_id": "s-abc"}),
    ("ota_update", {"url": "/api/v1/firmware/f-000001/download", "version": "0.2.0",
                    "sha256": "a" * 64, "size": 1203136}),
])
def test_api_commands_match_cmd_schema(action, params):
    assert errors("cmd.schema.json", command("cam-01", action, "x-1", params)) == []


def test_cmd_schema_rejects_bad_start_stream():
    msg = command("cam-01", "start_stream", "x-1", {"session_id": "s", "resolution": "uxga"})
    assert errors("cmd.schema.json", msg)


async def test_stream_session_sends_valid_start_stream(client, app, db):
    from app.models.node import Node

    db.add(Node(node_id="cam-05", kind="cam", node_state="operational"))
    await db.commit()
    assert (await client.post("/api/v1/stream/sessions", headers=USER,
                              json={"node_id": "cam-05"})).status_code == 201
    sent = app.state.hub.send_cmd[-1]
    msg = command(sent["node_id"], sent["action"], sent["cmd_id"], sent["params"])
    assert errors("cmd.schema.json", msg) == []


# ---------------------------------------------------------------- MQTT: lo que consume la API

def test_enums_match_mqtt_schemas():
    assert set(get_args(NodeState)) == enum_of("cam_status.schema.json", "node_state")
    assert set(get_args(CameraType)) == enum_of("cam_status.schema.json", "camera_type")
    assert set(get_args(EventType)) == enum_of("cam_event.schema.json", "event_type")
    assert set(get_args(Severity)) == enum_of("cam_event.schema.json", "severity")
    assert set(get_args(StreamResolution)) == set(
        load_schema("cmd.schema.json")["allOf"][0]["then"]["properties"]["params"]["properties"]
        ["resolution"]["enum"])


def test_api_accepts_schema_examples():
    for ex in load_schema("cam_status.schema.json")["examples"]:
        StatusMsg.model_validate(ex)
    for ex in load_schema("cam_event.schema.json")["examples"]:
        EventMsg.model_validate(ex)


def test_capture_metadata_example_is_accepted_by_api():
    from app.schemas.capture import CaptureMetadata

    for ex in load_schema("capture_metadata.schema.json")["examples"]:
        CaptureMetadata.model_validate(ex)
