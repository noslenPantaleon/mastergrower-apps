# API HTTP — Configuración (Contrato §5.4 y §7)

Base: `/api/v1`. Autenticación y errores como en [`captures.md`](captures.md).

## Modelo

Cada parámetro se resuelve como **override del nodo → global → valor por defecto**. Un nodo sin
override hereda el 100 % de la global; solo se guardan las excepciones (tablas `config_global` y
`config_node`). Parámetros de cámara, tipos, rangos y valores por defecto:
[`schemas/cam_config.schema.json`](schemas/cam_config.schema.json).

| Parámetro | Por defecto | Notas |
|---|---|---|
| `capture_interval_min` | 120 | 1–1440 |
| `light_window` | `{"start": "06:00", "end": "00:00"}` | hora local; `end` ≤ `start` cruza la medianoche. En el Contrato su alcance es la zona: cuando existan zonas (B4) se agrega ese nivel entre global y nodo |
| `utc_offset_min` | -180 | huso de la ventana de luz (agregado: el nodo no tiene zona horaria) |
| `analysis_mode` | `hybrid` | `hsv`, `hybrid`, `model` |
| `resolution` / `resolution_full` | `SVGA` / `UXGA` | rutina / prioridad alta |
| `jpeg_quality` | 12 | 4–63 (menor = mejor calidad) |
| `hsv_suspicion_threshold` | 0.35 | 0–1 |
| `min_trigger_interval_s` | 5 | anti-duplicado de disparos |
| `stream_resolution_max` · `stream_fps_max` · `stream_ttl_max_s` | `vga` · 10 · 600 | vista en vivo (C8) |

### Versión

Cada cambio (PUT o DELETE) crea una revisión numerada (`config_revisions`, con quién lo hizo y
qué cambió). La **versión** de la configuración de un nodo es el número de la última revisión que
lo afecta: una global o una de su override. Es un entero que solo crece; el nodo la guarda junto
con la config en flash y la compara para saber si tiene la última.

### Aviso a los nodos

Tras un cambio, la API publica en `growlab/cam/{id}/cmd` la acción `refresh_config` con
`params: {"version": N}` (a todos los nodos de cámara conocidos si el cambio es global, o solo al
nodo del override). Es un aviso de mejor esfuerzo: el nodo además consulta al arrancar y de forma
periódica.

## GET /config/global

Token de usuario o de nodo.

```json
{
  "version": 7,
  "updated_at": "2026-11-19T22:00:00Z",
  "config": { "capture_interval_min": 60, "light_window": {"start": "06:00", "end": "00:00"}, "...": "..." },
  "sources": { "capture_interval_min": "global", "light_window": "default", "...": "..." }
}
```

`version` es la última revisión global (0 si nunca se cambió). `sources` dice de dónde sale cada
valor: `default` o `global`.

## PUT /config/global

**Solo token de usuario.** Cuerpo: un objeto con los parámetros a cambiar (parcial). Un valor
`null` borra el valor global y vuelve al por defecto. Responde como `GET /config/global`.

| Respuesta | Cuándo |
|---|---|
| `200` | aplicado (si no cambió nada no se crea revisión) |
| `422` `invalid_data` | parámetro desconocido o valor fuera de rango; `detail` lista cada campo |

## GET /config/nodes/{node_id}

La consulta el firmware. Token de usuario, o el token **de ese mismo nodo**.

```json
{
  "node_id": "cam-01",
  "version": 9,
  "updated_at": "2026-11-19T22:05:00Z",
  "config": { "...": "configuración efectiva completa" },
  "sources": { "capture_interval_min": "override", "jpeg_quality": "global", "...": "default" }
}
```

Encabezado `ETag: "9"`. Si el nodo manda `If-None-Match: "9"` y no hubo cambios, responde
**`304`** sin cuerpo (consulta periódica barata).

Solo hay parámetros para nodos de cámara (`cam-NN`); para otros tipos `config` sale vacío hasta que
se definan los suyos.

## GET /config/nodes/{node_id}/override

Solo los valores que pisa el nodo: `{"node_id", "version", "updated_at", "override": {...}}`.

## PUT /config/nodes/{node_id}/override

**Solo token de usuario.** Igual que `PUT /config/global` pero sobre el nodo: parcial, `null` borra
ese override y vuelve al global. Responde como `GET /config/nodes/{id}`. `node_id` debe tener la
forma `cam-NN`, `ctrl-NN` o `disp-NN` (no hace falta que el nodo ya se haya conectado).

## DELETE /config/nodes/{node_id}/override

**Solo token de usuario.** Borra todo el override del nodo (vuelve 100 % a la global).
`204`; si no había override, `204` igual sin crear revisión.
