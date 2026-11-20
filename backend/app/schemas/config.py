"""Parámetros de configuración (contract/http/schemas/cam_config.schema.json) y respuestas."""

from datetime import datetime
from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field, ValidationError, field_serializer

from app.schemas.capture import Resolution
from app.schemas.common import iso_z

HHMM = r"^([01][0-9]|2[0-3]):[0-5][0-9]$"


class LightWindow(BaseModel):
    model_config = ConfigDict(extra="forbid")

    start: str = Field(pattern=HHMM)
    end: str = Field(pattern=HHMM)  # end <= start: la ventana cruza la medianoche


class CamParams(BaseModel):
    """Parámetros de un nodo de cámara con su valor por defecto (último nivel de la resolución).
    Deben coincidir con los defaults de Kconfig del firmware (lo verifica un test)."""

    model_config = ConfigDict(extra="forbid")

    capture_interval_min: int = Field(120, ge=1, le=1440)
    light_window: LightWindow = LightWindow(start="06:00", end="00:00")
    utc_offset_min: int = Field(-180, ge=-720, le=840)
    analysis_mode: Literal["hsv", "hybrid", "model"] = "hybrid"
    resolution: Resolution = "SVGA"
    resolution_full: Resolution = "UXGA"
    jpeg_quality: int = Field(12, ge=4, le=63)
    hsv_suspicion_threshold: float = Field(0.35, ge=0, le=1)
    min_trigger_interval_s: int = Field(5, ge=0, le=3600)
    stream_resolution_max: Literal["qvga", "vga"] = "vga"
    stream_fps_max: int = Field(10, ge=1, le=30)
    stream_ttl_max_s: int = Field(600, ge=10, le=3600)


# Parámetros por tipo de nodo (ctrl y disp todavía no tienen)
PARAMS_BY_KIND: dict[str, type[BaseModel]] = {"cam": CamParams}


def defaults(model: type[BaseModel]) -> dict[str, Any]:
    return model().model_dump(mode="json")


def validate_patch(
    model: type[BaseModel], patch: dict[str, Any]
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    """Valida un cambio parcial. Devuelve (valores normalizados, errores). None = borrar."""
    out: dict[str, Any] = {}
    errors: list[dict[str, Any]] = []
    for key, value in patch.items():
        if key not in model.model_fields:
            errors.append({"field": key, "error": "parámetro desconocido"})
            continue
        if value is None:
            out[key] = None
            continue
        try:
            out[key] = model.model_validate({key: value}).model_dump(mode="json")[key]
        except ValidationError as e:
            for err in e.errors():
                errors.append({"field": ".".join(str(p) for p in err["loc"]), "error": err["msg"]})
    return out, errors


Source = Literal["default", "global", "override"]


class _Versioned(BaseModel):
    version: int
    updated_at: datetime | None

    @field_serializer("updated_at")
    def _iso(self, dt: datetime | None) -> str | None:
        return iso_z(dt)


class GlobalConfigOut(_Versioned):
    config: dict[str, Any]
    sources: dict[str, Source]


class NodeConfigOut(_Versioned):
    node_id: str
    config: dict[str, Any]
    sources: dict[str, Source]


class NodeOverrideOut(_Versioned):
    node_id: str
    override: dict[str, Any]
