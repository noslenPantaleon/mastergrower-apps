# API HTTP y WebSocket — Vista en vivo (Contrato §5.6)

Base: `/api/v1`. Autenticación y errores como en [`captures.md`](captures.md).

La cámara no atiende conexiones entrantes: abre un WebSocket **hacia** la API y le manda cuadros
JPEG; la API los reparte a los espectadores (pantalla, app, navegador). Una cámara transmite una
sola sesión a la vez: si otro espectador pide la misma cámara se suma a la sesión que ya existe.

```
pantalla ── POST /stream/sessions ──► API ── cmd start_stream (MQTT) ──► cámara
pantalla ◄─ WS /stream/{id}/view ──── API ◄── WS /stream/{id}/publish ── cámara
```

## Objeto sesión

```json
{
  "session_id": "s-d1c90bd21bf8",
  "node_id": "cam-01",
  "requested_by": "pantalla",
  "resolution": "qvga",
  "fps_max": 10,
  "ttl_s": 120,
  "stream_state": "active",
  "end_reason": null,
  "started_at": "2026-11-22T18:00:00Z",
  "expires_at": "2026-11-22T18:02:00Z",
  "ended_at": null,
  "publisher_connected": true,
  "viewers": 1,
  "fps": 9.6,
  "frames": 50,
  "bytes": 108262,
  "dropped": 0
}
```

- `stream_state`: `requested` (esperando que la cámara se conecte), `active`, `ended`, `error`.
- `end_reason`: `stopped_by:<usuario>`, `ttl`, `no_viewers`, `publisher_closed`,
  `node_timeout` (la cámara no abrió el WebSocket a tiempo), `rejected:<reason del ack>`,
  `broker_unavailable`, `api_restart`, `api_shutdown`.
- `fps`: medido en la API sobre los últimos 5 s. `dropped`: cuadros descartados por espectadores
  lentos. `viewers`, `fps`, `publisher_connected` y `dropped` valen 0/false en una sesión
  terminada.

## POST /stream/sessions

**Solo token de usuario.**

```json
{ "node_id": "cam-01", "resolution": "qvga", "fps_max": 10, "ttl_s": 120 }
```

`resolution` (`qvga`, por defecto, o `vga`), `fps_max` (1–30) y `ttl_s` (10–3600, por defecto 120)
son opcionales y se **recortan** a los límites de la config del nodo (`stream_resolution_max`,
`stream_fps_max`, `stream_ttl_max_s`, ver [`config.md`](config.md)); la respuesta trae los valores
efectivos. La API publica `start_stream` (ver abajo) y responde:

```json
{ "...": "objeto sesión", "token": "<token de espectador>",
  "ws_view_url": "ws://192.168.1.169:8000/api/v1/stream/s-d1c90bd21bf8/view",
  "ws_publish_url": "/api/v1/stream/s-d1c90bd21bf8/publish" }
```

| Respuesta | Cuándo |
|---|---|
| `201` | sesión nueva |
| `200` | la cámara ya tenía una sesión abierta: se devuelve esa (mismo token) |
| `404` `not_found` | no existe una cámara con ese `node_id` |
| `409` `node_offline` | la cámara no está en línea (`node_state` `offline` o nunca visto) |
| `409` `stream_limit` | ya hay `STREAM_MAX_SESSIONS` sesiones abiertas (todas las cámaras) |
| `503` `unavailable` | la API no tiene conexión con el broker |

- `ws_view_url` usa el host por el que se pidió la sesión.
- `ws_publish_url` es absoluta si la API tiene `API_PUBLIC_URL`; si no, es una **ruta** que el nodo
  resuelve contra su propia URL de la API (`CONFIG_MG_API_BASE_URL`, `http` → `ws`).

## GET /stream/sessions · GET /stream/sessions/{id}

Token de usuario o de nodo. La lista acepta `node_id`, `live=true` (solo las abiertas) y `limit`
(1–200, 50 por defecto), y ordena de la más nueva a la más vieja. `GET /{id}`: el objeto sesión
o `404`.

## DELETE /stream/sessions/{id}

**Solo token de usuario.** Cierra la sesión, publica `stop_stream` a la cámara y cierra los
WebSocket. `204`; también si ya estaba terminada. `404` si no existe.

## WebSocket /stream/{id}/publish — la cámara

- Autenticación: token **del nodo** (`Authorization: Bearer`, o `?token=`); la sesión tiene que
  ser de ese nodo.
- Cada mensaje binario es un cuadro JPEG completo (empieza con `FF D8`, hasta
  `STREAM_MAX_FRAME_BYTES`, 256 KB por defecto); los que no cumplen se ignoran. Un mensaje de texto
  opcional (`{"ts", "seq"}`) puede preceder al cuadro; hoy la API no lo usa.
- Al conectarse la sesión pasa a `active`. Si el nodo cierra el WebSocket, la sesión termina
  (`publisher_closed`). Si se reconecta mientras la sesión sigue abierta, el WebSocket nuevo
  reemplaza al viejo.

## WebSocket /stream/{id}/view — el espectador

- Autenticación: el `token` de la sesión (`?token=` o `Authorization: Bearer`).
- Recibe mensajes binarios, cada uno un JPEG completo. Al conectarse recibe enseguida el último
  cuadro, si hay.
- **Espectador lento:** la API le guarda solo el cuadro más nuevo; si el anterior todavía no salió,
  lo descarta (nunca acumula atraso).
- Hasta `STREAM_MAX_VIEWERS` (4) por sesión.

Códigos de cierre de ambos WebSocket:

| Código | Significado |
|---|---|
| `1000` | la sesión terminó normalmente; `reason` = `end_reason` |
| `1011` | la sesión terminó con error |
| `4401` | token ausente o inválido |
| `4403` | la sesión es de otro nodo |
| `4409` | demasiados espectadores, o la cámara reconectó y este WebSocket quedó reemplazado |
| `4410` | la sesión no existe o ya terminó |

## Fin de la sesión

La sesión termina por `DELETE`, al vencer `ttl_s`, sin espectadores durante `STREAM_IDLE_S` (30 s),
si la cámara no se conecta en `STREAM_START_TIMEOUT_S` (15 s), si rechaza `start_stream` en su
`cmd/ack` o si cierra el WebSocket. Salvo en los dos últimos casos, la API publica `stop_stream`.
La cámara además aplica `ttl_s` por su cuenta.

## Comandos MQTT

`growlab/cam/{id}/cmd`, emisor `api` ([`../mqtt/schemas/cmd.schema.json`](../mqtt/schemas/cmd.schema.json)):

- `start_stream`, `cmd_id: "st-{session_id}"`,
  `params: {session_id, ws_publish_url, resolution, fps_max, ttl_s}`. El nodo responde
  `accepted`/`done` o `rejected` con `reason`.
- `stop_stream`, `cmd_id: "sp-{session_id}"`, `params: {session_id}`.

Mientras transmite, el nodo difiere los disparos por timer. Un `capture_now` pausa el stream, toma
la captura en resolución completa y lo retoma.

## Despliegue

El reparto vive en la memoria del proceso de la API: corre con **un solo worker** (como el
ingestor MQTT). Al reiniciar la API, las sesiones abiertas quedan `ended` / `api_restart`.
