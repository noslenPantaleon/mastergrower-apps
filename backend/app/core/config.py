"""Configuración de la API por variables de entorno (12-factor): la misma imagen corre en
desarrollo, Railway o AWS cambiando solo el entorno."""

from functools import lru_cache

from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env", extra="ignore")

    app_env: str = "dev"
    api_prefix: str = "/api/v1"

    database_url: str = "postgresql+asyncpg://mastergrow:mastergrow@localhost:5432/mastergrow"

    mqtt_host: str = "localhost"
    mqtt_port: int = 1883
    mqtt_api_user: str = "api"
    mqtt_api_password: str = ""


@lru_cache
def get_settings() -> Settings:
    return Settings()
