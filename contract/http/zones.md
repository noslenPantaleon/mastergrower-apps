# API HTTP — Zonas (Contrato §5.5 y §6)

Base: `/api/v1`. Autenticación y errores como en [`captures.md`](captures.md).

Una zona agrupa los nodos de un mismo espacio de cultivo (carpa, mesada). La pantalla agrupa
tarjetas y alarmas por zona, y `GET /captures`, `/events` y `/nodes` filtran por `zone_id`.

## Objeto zona

```json
{
  "zone_id": "zone-a",
  "name": "Carpa A",
  "grow_mode": "substrate",
  "light_cycle": { "start": "06:00", "end": "00:00" },
  "node_ids": ["cam-01", "ctrl-01"],
  "created_at": "2026-11-22T16:15:35Z",
  "updated_at": "2026-11-22T16:15:35Z"
}
```

- `zone_id`: minúsculas, dígitos y `-`, hasta 24 caracteres (`^[a-z0-9][a-z0-9-]{0,23}$`).
- `grow_mode` (opcional): `substrate`, `coco_coir` o `dwc` (SRS de control F-12).
- `light_cycle` (opcional): ventana de luz de la zona, mismo formato que el parámetro
  `light_window` de la configuración. Si está, los nodos de la zona la usan salvo que tengan un
  override propio (override → **zona** → global → default, ver [`config.md`](config.md)).
- `node_ids`: los nodos asignados (calculado).

## GET /zones

Token de usuario o de nodo. `{"items": [zona, ...]}` ordenado por `zone_id`.

## GET /zones/{zone_id}

La zona, o `404`.

## POST /zones

**Solo token de usuario.** `{"zone_id", "name", "grow_mode"?, "light_cycle"?}` → `201` con la
zona. `409` si ya existe, `422` si algún campo no cumple.

## PATCH /zones/{zone_id}

**Solo token de usuario.** Cambio parcial de `name`, `grow_mode` o `light_cycle`; `null` borra
`grow_mode` o `light_cycle` (`name` es obligatorio). Si cambia `light_cycle` se crea una revisión
de config (scope `zone:{zone_id}`) y la API publica `refresh_config` a las cámaras de la zona.

## DELETE /zones/{zone_id}

**Solo token de usuario.** `204`. `409` con `detail: {"node_ids": [...]}` si todavía tiene nodos
asignados (primero reasignarlos con `PUT /nodes/{id}/zone`).
