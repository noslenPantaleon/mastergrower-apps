# API HTTP — Nodos (Contrato §5.5 y §6)

Base: `/api/v1`. Autenticación y errores como en [`captures.md`](captures.md).

En B2 la tabla `nodes` se llena sola con lo que publican los nodos en el broker
(`growlab/+/+/status` y `growlab/ctrl/+/state`). El alta con emisión de token
(`POST /nodes`) y la asignación de zona (`PUT /nodes/{id}/zone`) llegan en B4.

## Objeto nodo

```json
{
  "node_id": "cam-01",
  "kind": "cam",
  "camera_type": "xiao-ov3660",
  "firmware": "0.1.0",
  "zone_id": null,
  "node_state": "operational",
  "last_seen": "2026-11-19T21:10:00Z",
  "status_ts": "2026-11-19T21:09:59Z",
  "last_status": { "rssi": -72, "sd_free_pct": 99, "last_capture": "2026-11-19T21:09:58Z", "...": "..." }
}
```

- `kind`: `cam`, `ctrl` o `disp` (sale del tema MQTT).
- `last_seen`: último mensaje recibido con el nodo en línea. Si el estado llegó como mensaje
  retenido (la API se reinició), se toma el `ts` del mensaje en vez de la hora de recepción.
- `status_ts`: el `ts` del último `status`/`state`.
- `last_status`: el último mensaje completo, tal cual lo mandó el nodo.
- `zone_id`: mientras no exista B4 se adopta el que informa el nodo, solo si la API no tiene uno.

## GET /nodes

Token de usuario (o de nodo). Filtros opcionales `kind`, `zone_id`, `node_state`.
Orden por `node_id`. Respuesta: `{"items": [nodo, ...]}` (sin paginar: son pocos).

## GET /nodes/{node_id}

El nodo, o `404`.
