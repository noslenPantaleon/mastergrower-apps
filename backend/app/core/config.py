"""Configuración de la API por variables de entorno (12-factor): la misma imagen corre en
desarrollo, Railway o AWS cambiando solo el entorno."""

from functools import lru_cache

from pydantic_settings import BaseSettings, SettingsConfigDict


def parse_pairs(value: str) -> dict[str, str]:
    """"a:x,b:y" → {"a": "x", "b": "y"} (ignora entradas vacías o mal formadas)."""
    pairs: dict[str, str] = {}
    for item in value.split(","):
        name, sep, secret = item.strip().partition(":")
        if sep and name and secret:
            pairs[name.strip()] = secret.strip()
    return pairs


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env", extra="ignore")

    app_env: str = "dev"
    api_prefix: str = "/api/v1"

    database_url: str = "postgresql+asyncpg://mastergrow:mastergrow@localhost:5432/mastergrow"

    mqtt_host: str = "localhost"
    mqtt_port: int = 1883
    mqtt_api_user: str = "api"
    mqtt_api_password: str = ""
    mqtt_client_id: str = "mastergrow-api"
    mqtt_ingest_enabled: bool = True   # B2: estado y eventos de los nodos → base

    # Tokens (Contrato §5.1). Formato "nombre:token,..." — B4 los pasa a la base con hash.
    api_node_tokens: str = ""   # node_id:token  (p. ej. cam-01:...)
    api_user_tokens: str = ""   # usuario:token  (pantalla, app, navegador)

    # Imágenes (ImageStore local; S3 más adelante)
    images_dir: str = "/data/images"
    max_image_bytes: int = 4 * 1024 * 1024
    thumb_max_px: tuple[int, int] = (320, 240)

    # Vista en vivo (B5, Contrato §5.6). Un solo proceso: el reparto de cuadros vive en memoria.
    # URL de la API vista desde los nodos (p. ej. http://192.168.1.169:8000).
    # Vacía = ws_publish_url relativa, que el nodo resuelve contra su propia URL de la API.
    api_public_url: str = ""
    stream_max_sessions: int = 2      # sesiones simultáneas (todas las cámaras)
    stream_max_viewers: int = 4       # espectadores por sesión
    stream_idle_s: int = 30           # sin espectadores durante este tiempo → se cierra
    stream_start_timeout_s: int = 15  # el nodo no abrió el WebSocket de publicación → error
    stream_max_frame_bytes: int = 256 * 1024

    @property
    def node_tokens(self) -> dict[str, str]:
        return parse_pairs(self.api_node_tokens)

    @property
    def user_tokens(self) -> dict[str, str]:
        return parse_pairs(self.api_user_tokens)


@lru_cache
def get_settings() -> Settings:
    return Settings()
