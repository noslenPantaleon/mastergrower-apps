#!/bin/sh
# Manda ota_update a uno o más nodos. Uso: ./scripts/fw_deploy.sh f-000001 cam-01 [cam-02 ...]
set -eu
cd "$(dirname "$0")/.."
[ $# -ge 2 ] || { echo "uso: $0 <firmware_id> <node_id> [node_id ...]"; exit 1; }
FW=$1; shift
set -a; . ./.env; set +a
TOK=$(echo "$API_USER_TOKENS" | cut -d, -f1 | cut -d: -f2)
NODES=$(printf '"%s",' "$@" | sed 's/,$//')
curl -s -H "Authorization: Bearer $TOK" -H 'Content-Type: application/json' \
     -d "{\"node_ids\":[$NODES]}" "http://localhost:8000/api/v1/firmware/$FW/deploy" | python3 -m json.tool
