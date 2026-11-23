"""Simulador del módulo de control (ctrl-01) mientras no existe el hardware real.

Publica growlab/ctrl/{id}/state (retenido, con LWT offline) según el contrato provisional
contract/mqtt/schemas/ctrl_state.schema.json, con valores que siguen un día de cultivo:
con la luz encendida suben lux y temperatura y baja el CO₂; el ventilador entra si hace calor;
el sustrato se seca y la bomba de riego lo repone; el reservorio baja y se rellena.

Uso (desde backend/, con docker compose arriba):
    uv run python scripts/ctrl_sim.py                       # sustrato, cada 10 s
    uv run python scripts/ctrl_sim.py --mode dwc --period 5
    uv run python scripts/ctrl_sim.py --hot                 # forzar calor (ver el ventilador)
Ctrl+C publica "offline" y termina.
"""

import argparse
import asyncio
import json
import math
import os
import random
import signal
from datetime import UTC, datetime, timedelta
from pathlib import Path

import aiomqtt


def env(name: str, default: str = "") -> str:
    if name in os.environ:
        return os.environ[name]
    p = Path(__file__).resolve().parents[1] / ".env"
    for line in p.read_text().splitlines() if p.exists() else []:
        k, _, v = line.partition("=")
        if k.strip() == name:
            return v.strip()
    return default


def iso(dt: datetime) -> str:
    return dt.strftime("%Y-%m-%dT%H:%M:%SZ")


def approach(value: float, target: float, rate: float, noise: float) -> float:
    """Primer orden hacia target + ruido: cambios suaves como un sensor real."""
    return value + (target - value) * rate + random.gauss(0, noise)


class Plant:
    def __init__(self, mode: str, light_on: str, light_off: str, utc_offset_min: int, hot: bool):
        self.mode = mode
        self.on_min = self._hhmm(light_on)
        self.off_min = self._hhmm(light_off)
        self.offset = timedelta(minutes=utc_offset_min)
        self.hot = hot
        self.temp, self.hum, self.co2, self.lux = 23.0, 62.0, 820.0, 0.0
        self.moisture, self.substrate_ec = 46.0, 1.6
        self.ph, self.ec, self.do, self.water_temp = 5.9, 1.8, 7.6, 20.5
        self.reservoir = 78.0
        self.pump_left = 0  # ciclos de riego restantes
        self.fan = False

    @staticmethod
    def _hhmm(s: str) -> int:
        h, m = s.split(":")
        return int(h) * 60 + int(m)

    def light_is_on(self, now: datetime) -> bool:
        local = now + self.offset
        m = local.hour * 60 + local.minute
        if self.on_min < self.off_min:
            return self.on_min <= m < self.off_min
        return m >= self.on_min or m < self.off_min  # cruza la medianoche (06:00–00:00)

    def cycle(self) -> str:
        on = (self.off_min - self.on_min) % 1440 or 1440
        return f"{on // 60}/{(1440 - on) // 60}"

    def step(self, now: datetime) -> dict:
        light = self.light_is_on(now)
        t_target = (25.5 if light else 21.0) + (4.0 if self.hot else 0.0)
        if self.fan:
            t_target -= 2.5
        self.temp = approach(self.temp, t_target, 0.08, 0.05)
        self.fan = self.temp > 26.5 or (self.fan and self.temp > 25.8)  # histéresis automática
        self.hum = approach(self.hum, 92 - 1.3 * self.temp - (6 if self.fan else 0), 0.1, 0.3)
        self.co2 = approach(self.co2, 620 if light else 980, 0.05, 6)
        self.lux = max(0.0, approach(self.lux, 18500 if light else 3, 0.5, 250 if light else 1))

        watering = self.mode != "dwc" and self.pump_left > 0
        if self.mode != "dwc":
            if self.moisture < 35 and self.pump_left == 0:
                self.pump_left = 4  # riego automático (F-01/F-09)
            if watering:
                self.moisture += 3.2
                self.reservoir -= 0.8
                self.pump_left -= 1
            self.moisture -= 0.08 + (0.04 if light else 0)  # se seca más rápido con luz
            self.substrate_ec = approach(
                self.substrate_ec, 1.6 + (46 - self.moisture) * 0.02, 0.05, 0.01
            )
        else:
            self.ph = approach(self.ph, 5.9 + 0.1 * math.sin(now.timestamp() / 3600), 0.05, 0.01)
            self.ec = approach(self.ec, 1.8, 0.05, 0.01)
            self.do = approach(self.do, 7.6, 0.05, 0.05)  # bomba de aire siempre encendida
            self.water_temp = approach(self.water_temp, self.temp - 3.5, 0.02, 0.03)
            self.reservoir -= 0.01
        if self.reservoir < 25:
            self.reservoir = 80.0  # relleno del reservorio

        r = lambda v, n=1: round(v, n)  # noqa: E731
        sensors = {
            "temperature": r(self.temp),
            "humidity": r(self.hum),
            "co2": round(self.co2),
            "lux": round(self.lux),
            "reservoir_level": round(self.reservoir),
        }
        if self.mode == "dwc":
            sensors |= {
                "substrate_moisture": None,
                "substrate_ec": None,
                "ph": r(self.ph, 2),
                "ec": r(self.ec, 2),
                "dissolved_oxygen": r(self.do),
                "water_temperature": r(self.water_temp),
            }
        else:
            sensors |= {
                "substrate_moisture": r(self.moisture),
                "substrate_ec": r(self.substrate_ec, 2),
                "ph": None,
                "ec": None,
                "dissolved_oxygen": None,
                "water_temperature": None,
            }
        actuators = [
            {"id": "light", "state": "on" if light else "off", "mode": "auto"},
            {"id": "fan", "state": "on" if self.fan else "off", "mode": "auto"},
        ]
        if self.mode == "dwc":
            actuators.append({"id": "air_pump", "state": "on", "mode": "auto"})
        else:
            actuators.append(
                {"id": "water_pump", "state": "on" if watering else "off", "mode": "auto"}
            )
        return {
            "sensors": sensors,
            "light": {"on": light, "cycle": self.cycle()},
            "actuators": actuators,
        }


async def main(a: argparse.Namespace) -> None:
    topic = f"growlab/ctrl/{a.node}/state"
    base = {
        "schema_version": 1,
        "node_id": a.node,
        "firmware": "sim-0.1",
        "zone_id": a.zone,
        "grow_mode": a.mode,
    }

    def offline() -> str:
        return json.dumps(
            base | {"ts": iso(datetime.now(UTC)), "node_state": "offline", "sensors": {}}
        )

    plant = Plant(a.mode, a.light_on, a.light_off, a.utc_offset, a.hot)
    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, stop.set)  # Ctrl+C: publicar offline antes de salir
    print(f"Simulando {a.node} ({a.mode}) en {topic} cada {a.period} s. Ctrl+C para terminar.")
    backoff = 2.0
    while not stop.is_set():
        try:
            will = aiomqtt.Will(topic, offline(), qos=1, retain=True)  # LWT si el proceso muere
            async with aiomqtt.Client(
                env("MQTT_HOST", "localhost"),
                int(env("MQTT_PORT", "1883")),
                username=env("MQTT_API_USER", "api"),
                password=env("MQTT_API_PASSWORD") or None,
                identifier=f"sim-{a.node}",
                will=will,
                keepalive=30,
            ) as client:
                backoff = 2.0
                while not stop.is_set():
                    now = datetime.now(UTC)
                    msg = base | {"ts": iso(now), "node_state": "operational"} | plant.step(now)
                    payload = json.dumps(msg, separators=(",", ":"))
                    await client.publish(topic, payload, qos=1, retain=True)
                    s = msg["sensors"]
                    hhmmss = (now + plant.offset).strftime("%H:%M:%S")
                    light = "ON" if msg["light"]["on"] else "OFF"
                    acts = "  ".join(f"{x['id']}={x['state']}" for x in msg["actuators"])
                    print(
                        f"{hhmmss}  {s['temperature']} °C  {s['humidity']} %  {s['co2']} ppm  "
                        f"{s['lux']} lx  luz {light}  {acts}",
                        flush=True,
                    )
                    try:
                        await asyncio.wait_for(stop.wait(), a.period)
                    except TimeoutError:
                        pass
                try:  # salida prolija: offline retenido (si falla, queda el LWT)
                    await client.publish(topic, offline(), qos=1, retain=True)
                    print("\nPublicado offline.")
                except aiomqtt.MqttError:
                    pass
        except aiomqtt.MqttError as e:
            if stop.is_set():
                break
            # El broker se cayó o la Mac se suspendió: reintentar como los nodos reales
            print(f"Sin conexión con el broker ({e}); reintento en {backoff:.0f} s", flush=True)
            try:
                await asyncio.wait_for(stop.wait(), backoff)
            except TimeoutError:
                pass
            backoff = min(backoff * 2, 30.0)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--node", default="ctrl-01")
    p.add_argument("--zone", default="zone-a")
    p.add_argument("--mode", choices=["substrate", "coco_coir", "dwc"], default="substrate")
    p.add_argument("--period", type=float, default=10, help="segundos entre mensajes")
    p.add_argument("--light-on", default="06:00")
    p.add_argument("--light-off", default="00:00")
    p.add_argument("--utc-offset", type=int, default=-180, help="minutos (Argentina: -180)")
    p.add_argument("--hot", action="store_true", help="sumar 4 °C para ver el ventilador")
    asyncio.run(main(p.parse_args()))
