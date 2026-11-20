# Nombres del contrato — equivalencia con el documento de referencia

El *Contrato de interfaces del sistema* (.docx en `references/`) usa nombres en español. Desde el
2026-11-18 el contrato ejecutable (`contract/`) y todo el código usan **inglés** en campos, valores,
rutas y tablas. Los comentarios, los logs y la documentación siguen en español.

## Campos

| Contrato .docx | Contrato ejecutable |
|---|---|
| `estado_nodo` | `node_state` |
| `ultima_captura` | `last_capture` |
| `sd_libre_pct` | `sd_free_pct` |
| `zona_id` | `zone_id` |
| `tipo_evento` | `event_type` |
| `severidad` | `severity` |
| `captura_id` · `evento_id` | `capture_id` · `event_id` |
| `detalle` | `detail` |
| `accion` | `action` |
| `parametros` | `params` |
| `resultado` | `result` |
| `motivo` | `reason` |
| `resolucion` | `resolution` |
| `prioridad` | `priority` |
| `sensores_snapshot` (`temperatura`, `humedad`, `lux`) | `sensor_snapshot` (`temperature`, `humidity`, `lux`) |
| `analisis_local` / `analisis_servidor` | `local_analysis` / `server_analysis` |
| `estado_subida` / `ruta_imagen` | `upload_state` / `image_path` |
| `estado_evento` · `timestamp_inicio` · `timestamp_resolucion` | `event_state` · `started_at` · `resolved_at` |
| `ultima_conexion` · `modo_cultivo` · `ciclo_luz` | `last_seen` · `grow_mode` · `light_cycle` |
| `tipo` (tabla nodos: cam, ctrl, disp) | `kind` |
| `intervalo_captura_min` · `ventana_luz` · `calidad_jpeg` · `umbral_sospecha_hsv` | `capture_interval_min` · `light_window` (`{start, end}`) · `jpeg_quality` · `hsv_suspicion_threshold` |
| `resolucion` (config) · `stream_resolucion_max` | `resolution` (+ `resolution_full`, agregado) · `stream_resolution_max` |
| tablas `config_global` · `config_nodo` (`clave`, `valor`, `actualizado`) | `config_global` · `config_node` (`key`, `value`, `updated_at`) + `config_revisions` (agregada) |
| `/config/global`, `/config/nodos/{id}/override` | `/config/global`, `/config/nodes/{id}/override` |

## Valores

| Enumeración | Contrato .docx | Contrato ejecutable |
|---|---|---|
| estado del nodo | `arrancando`, `operativo`, `autonomo`, `error`, `fuera_de_linea` | `booting`, `operational`, `autonomous`, `error`, `offline` |
| severidad | `baja`, `media`, `alta` | `low`, `medium`, `high` |
| tipo de evento | `luz_apagada_anticipada`, `anomalia_planta`, `sd_llena`, `error_camara`, `nodo_sin_conexion` | `early_lights_off`, `plant_anomaly`, `sd_full`, `camera_error`, `node_disconnected` |
| trigger_type | `timer`, `evento_sensor`, `comando_remoto`, `post_actuacion`, `calendario` | `timer`, `sensor_event`, `remote_command`, `post_actuation`, `schedule` |
| prioridad | `normal`, `alta` | `normal`, `high` |
| acción | `set_actuador`, `set_modo` (el resto ya en inglés) | `set_actuator`, `set_mode` |
| estado del evento | `abierto`, `reconocido`, `resuelto` | `open`, `acknowledged`, `resolved` |
| motivo de rechazo (ack) | — | `unsupported_schema_version`, `invalid_format`, `expired`, `unsupported_action`, `outside_light_window`, `min_interval`, `queue_full`, `camera_error`, `out_of_memory`, `config_unavailable` (refresh_config sin respuesta de la API) |

## Rutas HTTP y tablas

| Contrato .docx | Contrato ejecutable |
|---|---|
| `/capturas`, `/capturas/{id}/imagen` | `/captures`, `/captures/{id}/image` |
| `/eventos`, `/nodos`, `/zonas` | `/events`, `/nodes`, `/zones` |
| `/config/nodos/{id}` | `/config/nodes/{id}` |
| `/stream/sesiones` | `/stream/sessions` |
| tablas `capturas`, `eventos`, `nodos`, `zonas`, `stream_sesiones` | `captures`, `events`, `nodes`, `zones`, `stream_sessions` |
| paginación `limite` / `cursor` · filtros `desde` / `hasta` | `limit` / `cursor` · `since` / `until` |
| errores `codigo` / `mensaje` / `detalle` | `code` / `message` / `detail` |
| — (agregados) | evento: `acknowledged_at`, `acknowledged_by`, `received_at`; nodo: `status_ts`, `last_status`; error `conflict` (409) |

Los temas MQTT (`growlab/{kind}/{id}/{status|state|event|cmd|cmd/ack}`) no cambian.
