# Changelog del contrato

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
