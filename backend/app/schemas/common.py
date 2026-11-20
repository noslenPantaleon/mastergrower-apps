"""Enumeraciones del contrato y utilidades comunes de los esquemas."""

from datetime import UTC, datetime
from typing import Literal

CameraType = Literal["xiao-ov3660", "xiao-ov5640", "rpi-noir", "thermal-mlx90640", "thermal-lepton"]
NodeKind = Literal["cam", "ctrl", "disp"]
NodeState = Literal["booting", "operational", "autonomous", "error", "offline"]
Severity = Literal["low", "medium", "high"]
EventType = Literal[
    "early_lights_off", "plant_anomaly", "sd_full", "camera_error", "node_disconnected"
]
EventState = Literal["open", "acknowledged", "resolved"]


def utc(dt: datetime) -> datetime:
    # SQLite devuelve fechas sin zona: se asumen UTC
    return dt.replace(tzinfo=UTC) if dt.tzinfo is None else dt.astimezone(UTC)


def iso_z(dt: datetime | None) -> str | None:
    return None if dt is None else utc(dt).strftime("%Y-%m-%dT%H:%M:%SZ")
