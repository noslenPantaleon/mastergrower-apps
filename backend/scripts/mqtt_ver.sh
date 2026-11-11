#!/bin/sh
# Muestra todo el tráfico MQTT del sistema (usuario api). Uso: ./scripts/mqtt_ver.sh [filtro]
cd "$(dirname "$0")/.." && set -a && . ./.env && set +a
exec mosquitto_sub -h localhost -u "$MQTT_API_USER" -P "$MQTT_API_PASSWORD" -t "${1:-growlab/#}" -v
