# API HTTP — Nodos (Contrato §5.5, §6 y §8.2)

Base: `/api/v1`. Autenticación y errores como en [`captures.md`](captures.md).

La tabla `nodes` se llena con lo que publican los nodos en el broker (`growlab/+/+/status` y
`growlab/ctrl/+/state`) y con el alta (`POST /nodes`), que emite el token HTTP del nodo.

## Objeto nodo

```json
{
  "node_id": "cam-01",
  "kind": "cam",
  "camera_type": "xiao-ov3660",
  "firmware": "0.1.0",
  "zone_id": "zone-a",
  "node_state": "operational",
  "last_seen": "2026-11-19T21:10:00Z",
  "status_ts": "2026-11-19T21:09:59Z",
  "registered_at": "2026-11-22T16:15:19Z",
  "last_status": { "rssi": -72, "sd_free_pct": 99, "last_capture": "2026-11-19T21:09:58Z", "...": "..." }
}
```

- `kind`: `cam`, `ctrl` o `disp` (sale del prefijo del `node_id`).
- `last_seen`: último mensaje recibido con el nodo en línea. Si el estado llegó como mensaje
  retenido (la API se reinició), se toma el `ts` del mensaje en vez de la hora de recepción.
- `status_ts`: el `ts` del último `status`/`state`.
- `registered_at`: cuándo se dio de alta (`null` si solo se lo conoce por MQTT, sin token).
- `last_status`: el último mensaje completo, tal cual lo mandó el nodo.
- `zone_id`: la asigna la API (`PUT /nodes/{id}/zone`). El `zone_id` que informa el nodo en su
  `status` solo se adopta si el nodo no tiene zona y esa zona existe.

## Tokens de nodo

- Cada nodo tiene un token propio (`Authorization: Bearer`) para subir capturas y leer **su**
  configuración. La API guarda solo el SHA-256; el valor en claro se entrega **una única vez**
  (en el alta o al rotarlo) y se graba en el nodo (`CONFIG_MG_API_TOKEN`; NVS en el futuro).
- Un token revocado deja de valer de inmediato (`401`).
- Tokens previos en el entorno (`API_NODE_TOKENS`): la API los importa a la base al arrancar,
  solo si nunca los vio (ni revocados) y el nodo no tiene otro vigente. Revocar o rotar desde la
  API no se deshace al reiniciar; conviene borrarlos del `.env` después.
- El usuario y la contraseña MQTT del nodo (broker) se siguen gestionando aparte, en Mosquitto.

## GET /nodes

Token de usuario (o de nodo). Filtros opcionales `kind`, `zone_id`, `node_state`.
Orden por `node_id`. Respuesta: `{"items": [nodo, ...]}` (sin paginar: son pocos).

## GET /nodes/{node_id}

El nodo, o `404`.

## POST /nodes

**Solo token de usuario.** Da de alta un nodo y entrega su token.

```json
{ "node_id": "cam-02", "camera_type": "xiao-ov3660", "zone_id": "zone-a" }
```

`camera_type` (solo `cam`) y `zone_id` son opcionales. Si el nodo ya era conocido por MQTT se
completa ese registro.

| Respuesta | Cuándo |
|---|---|
| `201` | `{"node_id", "token", "node": {nodo}}` |
| `409` `conflict` | el nodo ya tiene un token vigente (usar `POST /nodes/{id}/token`) |
| `422` `invalid_data` | `node_id` sin la forma `cam-NN`/`ctrl-NN`/`disp-NN`, `camera_type` en un nodo que no es cámara, o zona inexistente |

## POST /nodes/{node_id}/token

**Solo token de usuario.** Revoca los tokens vigentes del nodo y emite uno nuevo:
`201` `{"node_id", "token"}`. `404` si el nodo no existe.

## DELETE /nodes/{node_id}/token

**Solo token de usuario.** Revoca los tokens del nodo (no lo borra: sigue en la lista con su
historial). `204`; `404` si no existe.

## PUT /nodes/{node_id}/zone

**Solo token de usuario.** `{"zone_id": "zone-a"}` asigna, `{"zone_id": null}` quita la zona.
Responde el nodo. `404` si el nodo no existe, `422` si la zona no existe.

Como la zona aporta parámetros de configuración (ventana de luz, ver [`config.md`](config.md)),
cambiar de zona crea una revisión de config del nodo y la API le publica `refresh_config`.
Asignar la zona que ya tiene no hace nada.
