"""Valida en vivo el tráfico MQTT contra contract/mqtt/schemas (B6).

Escucha growlab/# con el usuario de la API y valida cada mensaje según su canal. Sirve para
comprobar el firmware (cámara, y después pantalla y control) sin correr su código en pytest.

Uso (desde backend/, con el broker de docker compose corriendo):
    uv run python scripts/mqtt_validate.py [segundos] [--retained-only]
Sale con código 1 si algún mensaje no cumple.
"""

import asyncio
import json
import os
import sys
from collections import Counter
from pathlib import Path

import aiomqtt
from jsonschema import Draft202012Validator, FormatChecker

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from app.contract import CHANNEL_SCHEMA, load_schema  # noqa: E402


def env(name: str, default: str = "") -> str:
    if name in os.environ:
        return os.environ[name]
    p = Path(__file__).resolve().parents[1] / ".env"
    for line in p.read_text().splitlines() if p.exists() else []:
        k, _, v = line.partition("=")
        if k.strip() == name:
            return v.strip()
    return default


async def main(seconds: float) -> int:
    validators = {k: Draft202012Validator(load_schema(v), format_checker=FormatChecker())
                  for k, v in CHANNEL_SCHEMA.items()}
    ok, bad, skipped = Counter(), Counter(), Counter()
    async with aiomqtt.Client(env("MQTT_HOST", "localhost"), int(env("MQTT_PORT", "1883")),
                              username=env("MQTT_API_USER", "api"),
                              password=env("MQTT_API_PASSWORD") or None,
                              identifier=f"mastergrow-validate-{os.getpid()}") as client:
        await client.subscribe("growlab/#", qos=1)
        print(f"Escuchando growlab/# durante {seconds:.0f} s…")
        try:
            async with asyncio.timeout(seconds):
                async for msg in client.messages:
                    topic = msg.topic.value
                    parts = topic.split("/", 3)
                    key = (parts[1], parts[3]) if len(parts) == 4 else None
                    v = validators.get(key)
                    if v is None:
                        skipped[topic] += 1
                        continue
                    label = f"{parts[1]}/{parts[3]}"
                    try:
                        data = json.loads(bytes(msg.payload))
                    except ValueError:
                        bad[label] += 1
                        print(f"✗ {topic}: no es JSON")
                        continue
                    errors = sorted(v.iter_errors(data), key=lambda e: list(e.path))
                    if errors:
                        bad[label] += 1
                        for e in errors:
                            field = ".".join(str(p) for p in e.path) or "(raíz)"
                            print(f"✗ {topic} {field}: {e.message}")
                    else:
                        ok[label] += 1
                        print(f"✓ {topic}{' (retenido)' if msg.retain else ''}")
        except TimeoutError:
            pass
    print("\nResumen:")
    for label in sorted(set(ok) | set(bad)):
        print(f"  {label:14} {ok[label]:4} ok  {bad[label]:3} con errores")
    if skipped:
        print(f"  sin esquema todavía: {', '.join(sorted(skipped))}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main(float(sys.argv[1]) if len(sys.argv) > 1 else 60)))
