# API HTTP — Firmware y OTA (plan C9 / D6)

Base: `/api/v1`. Autenticación y errores como en [`captures.md`](captures.md).

Los nodos se actualizan por OTA: la API guarda las imágenes de firmware y les manda el comando
MQTT `ota_update`; cada nodo descarga la imagen por HTTP, la verifica, la graba en la partición
OTA libre y reinicia. El firmware nuevo arranca **a prueba** y se confirma solo cuando vuelve a
conectar con el broker y la API; si no, el bootloader vuelve a la versión anterior (rollback).

## Objeto firmware

```json
{
  "firmware_id": "f-000003",
  "kind": "cam",
  "version": "0.2.0",
  "project": "camera_node",
  "idf_ver": "v6.1",
  "size": 1203136,
  "sha256": "9f2c…",
  "uploaded_by": "dev",
  "created_at": "2026-11-22T21:00:00Z",
  "download_url": "/api/v1/firmware/f-000003/download"
}
```

`version`, `project` e `idf_ver` **se leen del propio `.bin`** (`esp_app_desc_t`, en el offset
32 de la imagen): no se escriben a mano. `project` define `kind`: `camera_node` → `cam`,
`display_node` → `disp`, `control_node` → `ctrl`. La versión sale de `version.txt` de cada
proyecto (`PROJECT_VER` de ESP-IDF).

## POST /firmware

**Solo token de usuario.** Multipart con `file` = `build/<proyecto>.bin`.

| Respuesta | Cuándo |
|---|---|
| `201` | firmware guardado |
| `409` `conflict` | ya hay uno con ese `kind` y `version` |
| `413` | supera `FIRMWARE_MAX_BYTES` (6 MB) |
| `422` | no es una imagen de app de ESP-IDF o el proyecto no es de un nodo conocido |

## GET /firmware · GET /firmware/{kind}/latest

Token de usuario o de nodo. Lista (filtro `kind`), del más nuevo al más viejo; `latest` devuelve
el último subido de ese tipo o `404`.

## GET /firmware/{firmware_id}/download

Token de usuario o de nodo. `application/octet-stream`, con `X-Firmware-Version` y
`X-Firmware-Sha256`.

## POST /firmware/{firmware_id}/deploy

**Solo token de usuario.** `{"node_ids": ["cam-01", ...]}` → publica `ota_update` a cada nodo y
responde `{"firmware_id", "version", "results": [{"node_id", "cmd_id", "sent", "error"}]}`.
`error`: `not_found`, `wrong_kind` (el nodo no es del tipo del firmware), `node_offline`,
`broker_unavailable`.

## DELETE /firmware/{firmware_id}

**Solo token de usuario.** Borra la imagen y el registro. `204`.

## Comando MQTT `ota_update`

`growlab/{kind}/{id}/cmd`, emisor `api`, `cmd_id: "ota-{n}-{node_id}"`:

```json
{ "action": "ota_update", "params": { "url": "/api/v1/firmware/f-000003/download",
  "version": "0.2.0", "sha256": "9f2c…", "size": 1203136 } }
```

- El nodo responde `accepted` y descarga en segundo plano. Si falla: `rejected` con `reason`:
  `same_version` (ya la tiene), `wrong_project` (la imagen es de otro tipo de nodo),
  `download_failed`, `bad_checksum` (SHA-256 distinto), `busy` (ya hay una actualización en curso).
- Si todo sale bien reinicia con la versión nueva y, **después de confirmarla** (conectó con el
  broker y la API), manda `done` con el mismo `cmd_id` y publica el `firmware` nuevo en su status.
- Si el firmware nuevo no logra conectarse en 3 minutos, el nodo vuelve solo a la versión anterior
  y, al reconectar con esa, manda `rejected` con `reason: "rolled_back"` para el mismo `cmd_id`.
