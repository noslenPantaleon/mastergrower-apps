# API HTTP — Capturas (Contrato §5.1 y §5.2)

Base: `/api/v1`. Errores: código HTTP + cuerpo `{"code": "...", "message": "...", "detail": ...}`.

## Autenticación

`Authorization: Bearer <token>`.

| Token | Quién | Puede |
|---|---|---|
| de nodo | cada nodo de cámara (hoy en Kconfig; en C6/B4 en NVS, emitido por `POST /nodes`) | `POST /captures` solo con su propio `node_id` |
| de usuario | pantalla, app, navegador | lecturas (`GET`) |

Para `<img src>` en un navegador, las lecturas aceptan también `?token=<token de usuario>`.

## POST /captures

`multipart/form-data` con dos partes:

| Parte | Tipo | Contenido |
|---|---|---|
| `image` | `image/jpeg` | JPEG original (máx. 4 MB) |
| `metadata` | `application/json` | [`schemas/capture_metadata.schema.json`](schemas/capture_metadata.schema.json) |

Encabezado opcional **`Idempotency-Key`** (≤ 64 caracteres; el nodo usa el id local de la SD).
Si el nodo reintenta porque se perdió la respuesta, la API no duplica la captura.

| Respuesta | Cuándo | Qué hace el nodo |
|---|---|---|
| `201` `{"capture_id": "c-000123", ...}` | creada | borra la copia local |
| `200` mismo cuerpo | ya existía (misma `Idempotency-Key` del mismo nodo) | borra la copia local |
| `400`, `413`, `415`, `422` | datos inválidos (no se arregla reintentando) | descarta la captura y lo registra |
| `401`, `403` | token inválido o `node_id` ajeno | reintenta con espera larga |
| `5xx` / sin red | — | reintenta con backoff exponencial |

Si `timestamp` es `null`, la API usa la hora de recepción y responde `timestamp_approx: true`.

Códigos de error (`code`): `bad_request`, `unauthenticated`, `forbidden`, `not_found`,
`payload_too_large`, `unsupported_media_type`, `invalid_data`.

## GET /captures

Filtros: `node_id`, `trigger_type`, `since`, `until` (ISO-8601), `zone_id` (cuando existan zonas, B4).
Paginación: `limit` (1–200, por defecto 50) y `cursor` (el `next_cursor` de la página anterior).
Orden: más recientes primero. Respuesta `{"items": [Capture...], "next_cursor": "..." | null}`.

## GET /captures/{id} · /captures/{id}/image · /captures/{id}/thumb

Metadatos (`Capture`), JPEG original, y miniatura JPEG de hasta 320×240 generada y cacheada por la API.
