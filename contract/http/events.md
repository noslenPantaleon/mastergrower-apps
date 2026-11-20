# API HTTP — Eventos (Contrato §5.3 y §6)

Base: `/api/v1`. Autenticación y errores como en [`captures.md`](captures.md).

## Origen de los eventos

La API se suscribe al broker (`growlab/+/+/event`, QoS 1, sesión persistente) y guarda cada
evento en la tabla `events` con `event_state: "open"`. Valida el mensaje contra
[`../mqtt/schemas/cam_event.schema.json`](../mqtt/schemas/cam_event.schema.json). Los mensajes
inválidos o con un `node_id` que no coincide con el del tema se descartan (y quedan en el log).

- Duplicados: QoS 1 puede entregar dos veces. Un evento con el mismo `node_id`, `event_type` y
  `ts` que uno ya guardado se ignora.
- Sin hora: si `ts` es anterior a 2020 (el nodo todavía no sincronizó NTP), se usa la hora de
  recepción.
- **`node_disconnected` lo genera la API**, no el nodo. Cuando llega un `status` con
  `node_state: "offline"` (el LWT), la API abre un evento `node_disconnected` (severidad
  `medium`) si no hay uno abierto para ese nodo. Cuando el nodo vuelve a publicar otro estado,
  la API lo resuelve sola.

## Objeto evento

```json
{
  "event_id": "e-000042",
  "node_id": "cam-01",
  "event_type": "sd_full",
  "severity": "medium",
  "event_state": "open",
  "started_at": "2026-11-19T21:10:00Z",
  "acknowledged_at": null,
  "acknowledged_by": null,
  "resolved_at": null,
  "capture_id": null,
  "detail": "SD al 81 %",
  "received_at": "2026-11-19T21:10:01Z"
}
```

`event_state`: `open` → `acknowledged` → `resolved` (también se puede pasar de `open` a
`resolved` sin reconocer).

## GET /events

Token de usuario (o de nodo). Orden: más nuevo primero (`started_at`, luego `event_id`).

| Parámetro | Descripción |
|---|---|
| `node_id` | filtra por nodo |
| `zone_id` | nodos de esa zona (según la tabla `nodes`) |
| `severity` | `low`, `medium`, `high` |
| `event_state` | `open`, `acknowledged`, `resolved` |
| `event_type` | uno de los tipos de evento |
| `since`, `until` | rango sobre `started_at` (`until` excluido) |
| `limit` (1–200, por defecto 50), `cursor` | paginación; la respuesta trae `next_cursor` |

Respuesta: `{"items": [evento, ...], "next_cursor": "..." | null}`.

## GET /events/{event_id}

El evento, o `404`.

## PATCH /events/{event_id}

**Solo token de usuario.** Cuerpo: `{"event_state": "acknowledged" | "resolved"}`.

| Respuesta | Cuándo |
|---|---|
| `200` evento actualizado | transición válida, o el evento ya estaba en ese estado |
| `409` `conflict` | el evento ya está `resolved` y se pide `acknowledged` |
| `403` | token de nodo |
| `404` | no existe |
| `422` | `event_state` inválido |

Al reconocer se guarda `acknowledged_at` y `acknowledged_by` (el nombre del usuario del token);
al resolver, `resolved_at`.
