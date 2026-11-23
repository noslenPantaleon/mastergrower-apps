#!/bin/sh
# Abre en el navegador las páginas web de la API con el token de usuario (sin copiarlo a mano).
# Uso: ./scripts/open_web.sh gallery | live [node_id] | docs
# Ojo al grabar: el token queda en la barra de direcciones.
set -eu
cd "$(dirname "$0")/.."
set -a; . ./.env; set +a
TOK=$(echo "$API_USER_TOKENS" | cut -d, -f1 | cut -d: -f2)
case "${1:-gallery}" in
    gallery) open "http://localhost:8000/gallery?token=$TOK" ;;
    live)    open "http://localhost:8000/live?token=$TOK&node=${2:-cam-01}" ;;
    docs)    open "http://localhost:8000/docs" ;;
    *)       echo "uso: $0 gallery | live [node_id] | docs"; exit 1 ;;
esac
