#!/bin/sh
# Envía un comando a un nodo de cámara según el contrato.
# Uso: ./scripts/cam_cmd.sh <action> [node_id] [params_json]
#   ./scripts/cam_cmd.sh capture_now
#   ./scripts/cam_cmd.sh reboot cam-01
set -eu
cd "$(dirname "$0")/.." && set -a && . ./.env && set +a
ACTION=${1:?"falta la acción (capture_now, reboot, refresh_config, ...)"}
NODE=${2:-cam-01}
PARAMS=${3:-}
CMD_ID="cli-$(date +%s)"
TS=$(date -u +%Y-%m-%dT%H:%M:%SZ)
MSG="{\"schema_version\":1,\"node_id\":\"api\",\"ts\":\"$TS\",\"action\":\"$ACTION\",\"cmd_id\":\"$CMD_ID\"${PARAMS:+,\"params\":$PARAMS}}"

echo "→ growlab/cam/$NODE/cmd  $MSG"
mosquitto_sub -h localhost -u "$MQTT_API_USER" -P "$MQTT_API_PASSWORD" -t "growlab/cam/$NODE/cmd/ack" -W 5 &
SUB=$!
sleep 0.3
mosquitto_pub -h localhost -u "$MQTT_API_USER" -P "$MQTT_API_PASSWORD" -q 1 -t "growlab/cam/$NODE/cmd" -m "$MSG"
wait $SUB 2>/dev/null || true
