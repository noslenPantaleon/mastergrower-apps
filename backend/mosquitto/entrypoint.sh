#!/bin/sh
# Genera el archivo de contraseñas (hash) a partir de las variables de entorno y arranca el broker.
set -eu
PASSWD=/mosquitto/data/passwd
: > "$PASSWD"
chmod 0700 "$PASSWD"
mosquitto_passwd -b "$PASSWD" "$MQTT_API_USER" "$MQTT_API_PASSWORD"

OLD_IFS=$IFS; IFS=','
for pair in ${MQTT_NODE_USERS:-}; do
    user=${pair%%:*}; pass=${pair#*:}
    [ -n "$user" ] && [ "$user" != "$pair" ] && mosquitto_passwd -b "$PASSWD" "$user" "$pass"
done
IFS=$OLD_IFS

chown mosquitto:mosquitto "$PASSWD"
exec mosquitto -c /mosquitto/config/mosquitto.conf
