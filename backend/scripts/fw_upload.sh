#!/bin/sh
# Sube un firmware a la API (la versión y el tipo de nodo los lee la API del propio .bin).
# Uso: ./scripts/fw_upload.sh ../camera_node/build/camera_node.bin
set -eu
cd "$(dirname "$0")/.."
[ -f "${1:-}" ] || { echo "uso: $0 <archivo.bin>"; exit 1; }
set -a; . ./.env; set +a
TOK=$(echo "$API_USER_TOKENS" | cut -d, -f1 | cut -d: -f2)
curl -s -H "Authorization: Bearer $TOK" -F "file=@$1;type=application/octet-stream" \
     "http://localhost:8000/api/v1/firmware" | python3 -m json.tool
