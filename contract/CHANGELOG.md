# Changelog del contrato

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
