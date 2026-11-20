# Temas MQTT — contrato v1

Fuente: *Contrato de interfaces del sistema*, §4. Patrón: `growlab/{kind}/{node_id}/{channel}`,
con `kind` ∈ `cam`, `ctrl`, `disp`.

| Tema | Publica | Consume | QoS | Retenido | Esquema |
|---|---|---|---|---|---|
| `growlab/cam/{id}/status` | nodo cámara (LWT `offline`) | pantalla, API | 1 | sí | `schemas/cam_status.schema.json` |
| `growlab/cam/{id}/event` | nodo cámara | pantalla, API | 1 | no | `schemas/cam_event.schema.json` |
| `growlab/cam/{id}/cmd` | pantalla, API | nodo cámara | 1 | no | `schemas/cmd.schema.json` |
| `growlab/cam/{id}/cmd/ack` | nodo cámara | emisor del comando | 1 | no | `schemas/cmd_ack.schema.json` |
| `growlab/ctrl/{id}/state` | módulo de control | pantalla, API | 1 | sí | *(provisional, §4.4)* |
| `growlab/ctrl/{id}/event` | módulo de control | pantalla, API | 1 | no | *(provisional)* |
| `growlab/ctrl/{id}/cmd` | pantalla | módulo de control | 1 | no | `schemas/cmd.schema.json` |
| `growlab/ctrl/{id}/cmd/ack` | módulo de control | pantalla | 1 | no | `schemas/cmd_ack.schema.json` |
| `growlab/disp/{id}/status` | pantalla (opcional) | API | 1 | sí | *(pendiente)* |

## Reglas comunes

- Todo payload es JSON UTF-8 con `schema_version`, `node_id` y `ts` (ISO 8601 UTC, `Z`).
- Los receptores ignoran los campos que no conocen; los campos nuevos son opcionales.
- Comandos: `cmd_id` lo genera el emisor; el nodo descarta `cmd_id` ya ejecutados y comandos con
  `ts` más viejo que la ventana de vigencia (60 s).
- Imágenes nunca por MQTT: siempre por HTTP (`POST /api/v1/captures`).

Nombres de campos y valores: ver [`../NAMING.md`](../NAMING.md) (equivalencia con el Contrato .docx).
