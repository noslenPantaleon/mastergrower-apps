# Changelog del contrato

## [contrato B / schema_version 1] — 2026-11-22 (B6)
- `contract/openapi.json` generado por FastAPI (`backend/scripts/export_contract.py`); un test
  falla si queda desactualizado respecto de la API.
- Tests de contrato (`backend/tests/test_contract.py`): esquemas válidos con ejemplos que
  cumplen, comandos de la API contra `cmd.schema.json`, enums de la API iguales a los esquemas.
- `backend/scripts/mqtt_validate.py`: valida en vivo el tráfico del broker contra los esquemas
  (así se comprueba el firmware de cada nodo).

## [contrato B / schema_version 1] — 2026-11-22 (C8)
- Nodo de cámara: `start_stream` responde `accepted` y, cuando envió el primer cuadro, `done`;
  si no pudo abrir el WebSocket, `rejected` con `reason: "stream_connect_failed"` (o
  `camera_error`). Si ya transmite esa sesión: `done` + `already_streaming`.
- `stop_stream` responde `done`; si no había esa sesión, `done` + `not_streaming`.
- Un `start_stream` de otra sesión reemplaza a la que esté transmitiendo.

## [contrato B / schema_version 1] — 2026-11-22 (B5)
- HTTP: `POST/GET /stream/sessions`, `GET/DELETE /stream/sessions/{id}`; WebSocket
  `/stream/{id}/publish` (token del nodo) y `/stream/{id}/view` (token de la sesión)
  ([`http/stream.md`](http/stream.md)). Códigos de cierre 4401/4403/4409/4410.
- Estados `requested`, `active`, `ended`, `error` y `end_reason`. Tabla `stream_sessions`.
- Una sesión por cámara: un segundo pedido se suma a la existente (`200`). Los parámetros
  pedidos se recortan a `stream_resolution_max`, `stream_fps_max` y `stream_ttl_max_s` del nodo.
- `start_stream.params.ws_publish_url` puede ser una ruta relativa; `stop_stream` lleva
  `params.session_id`. Un `cmd/ack` `rejected` de `start_stream` termina la sesión con `error`.
- Códigos de error nuevos: `node_offline`, `stream_limit` (409) y `unavailable` (503).

## [contrato B / schema_version 1] — 2026-11-22 (B4)
- HTTP: `POST /nodes` (alta con emisión de token), `POST/DELETE /nodes/{id}/token` (rotar y
  revocar), `PUT /nodes/{id}/zone` ([`http/nodes.md`](http/nodes.md)); `GET/POST /zones`,
  `GET/PATCH/DELETE /zones/{id}` ([`http/zones.md`](http/zones.md)).
- Tokens de nodo en la base (tabla `node_tokens`, solo el hash), revocables. Los de
  `API_NODE_TOKENS` se importan al arrancar.
- Zona: `zone_id`, `name`, `grow_mode` (`substrate`, `coco_coir`, `dwc`), `light_cycle`
  (`{start, end}`). Nodo: agregado `registered_at`.
- Config: nivel **zona** entre global y override para `light_window` (`sources` puede valer
  `zone`). Cambiar la ventana de una zona o mover un nodo de zona crea una versión nueva y
  publica `refresh_config`.
- El `zone_id` del `status` de un nodo ya no crea zonas: solo se adopta si existe y el nodo no
  tiene una.

## [contrato B / schema_version 1] — 2026-11-20 (C7)
- `local_analysis` de los metadatos de captura definido (HSV): `green_ratio`, `yellow_brown_ratio`,
  `brightness`, `dark_ratio`, `dark_scene`, `low_plant_coverage`, `suspicion_score`, `threshold`,
  `suspicious`, `mode`, `requested_mode`, `analyzed_size`, `elapsed_ms`.
- Eventos `plant_anomaly` (al pasar a sospechoso; severidad `high` desde puntaje 0.6) y
  `early_lights_off` (escena oscura dentro de la ventana de luz), uno por episodio.
  `capture_id` va `null`: el nodo no conoce el id de la API al momento del evento.
- Una captura sospechosa o que generó un evento se sube con prioridad (id local `_h_`); el campo
  `priority` de los metadatos sigue indicando la prioridad del disparo.

## [contrato B / schema_version 1] — 2026-11-20 (C6)
- `cam_status`: campo opcional `config_version` (versión de la config remota aplicada).
- `refresh_config`: `params.version` opcional; si ya es la vigente el nodo responde `done` con
  `reason: "already_current"` sin consultar. Si no puede consultar la API: `rejected` con
  `reason: "config_unavailable"`.

## [contrato B / schema_version 1] — 2026-11-20
- HTTP: `GET/PUT /config/global`, `GET /config/nodes/{id}`, `GET/PUT/DELETE
  /config/nodes/{id}/override` ([`http/config.md`](http/config.md)).
- Parámetros de cámara con tipos, rangos y defaults en
  [`http/schemas/cam_config.schema.json`](http/schemas/cam_config.schema.json). Agregados respecto
  del Contrato §7: `utc_offset_min`, `resolution_full`, `min_trigger_interval_s`.
  `light_window` es un objeto `{start, end}` (HH:MM, hora local).
- Versión de configuración = número de la última revisión que afecta al nodo (tabla
  `config_revisions`). `ETag` / `If-None-Match` → `304` en `GET /config/nodes/{id}`.
- La API publica `refresh_config` con `params.version` a los nodos afectados tras cada cambio.

## [contrato B / schema_version 1] — 2026-11-19
- HTTP: `GET /events`, `GET/PATCH /events/{id}` ([`http/events.md`](http/events.md)) y
  `GET /nodes`, `GET /nodes/{id}` de solo lectura ([`http/nodes.md`](http/nodes.md)).
- La API genera `node_disconnected` al recibir el LWT (`node_state: "offline"`) y lo resuelve
  cuando el nodo vuelve.
- Agregados compatibles: `acknowledged_at`, `acknowledged_by`, `received_at` en eventos;
  `status_ts`, `last_status` en nodos; código de error `conflict` (409).
- `GET /captures?zone_id=` ya filtra (por la zona actual del nodo en la tabla `nodes`).

## [contrato B / schema_version 1, nombres en inglés] — 2026-11-18
- **Todos los nombres del contrato pasan a inglés** (campos, valores de enumeraciones, rutas HTTP,
  códigos de error, tablas). Equivalencia con el .docx en [`NAMING.md`](NAMING.md).
- Se mantiene `schema_version` 1: el cambio se hace antes de cualquier despliegue y no hay
  consumidores con los nombres anteriores (firmware, API y scripts se actualizan juntos).
- `contract/http/capturas.md` → `captures.md`; `captura_metadatos.schema.json` → `capture_metadata.schema.json`.
- Ids locales de la SD: la letra de prioridad pasa de `a`/`n` a `h`/`n`.

## [contrato B / schema_version 1] — 2026-11-17
- HTTP: `POST/GET /capturas`, `/capturas/{id}`, `/imagen`, `/thumb` (hoy `http/captures.md`).
- Esquema de los metadatos de captura (hoy `http/schemas/capture_metadata.schema.json`).
- Agregados compatibles: encabezado `Idempotency-Key` en `POST /capturas`, `timestamp: null` cuando el
  nodo no tenía hora (la API responde `timestamp_approx: true`), campos opcionales `ancho`, `alto`, `seq`.

## [contrato B / schema_version 1] — 2026-11-10
- Primera versión ejecutable: temas MQTT del nodo de cámara (`status`, `event`, `cmd`, `cmd/ack`).
- Enumeraciones tomadas del Contrato §3.3. Ante diferencias con el SRS de cámara
  (severidad, trigger_type, estado_evento, event_type), prevalece el Contrato.
