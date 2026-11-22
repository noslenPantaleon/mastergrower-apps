# Plan de desarrollo — MasterGrow (Nodo de Cámara, Nodo de Pantalla y API)

Basado en: SRS Nodo de Cámara, SRS Nodo de Pantalla, Arquitectura Nodo de Cámara (D),
Arquitectura Nodo de Pantalla (B) y Contrato de interfaces (B). El **Contrato** es la
referencia única de temas, mensajes y endpoints.

> **Nombres (2026-11-18):** el código y el contrato ejecutable usan nombres en **inglés**
> (campos, enums, rutas, tablas, archivos). Este plan conserva los nombres en español del
> Contrato .docx; la equivalencia está en `contract/NAMING.md`.

Entorno: ESP-IDF **v6.1** (`~/.espressif/v6.1/esp-idf`), targets `esp32s3`.

---

## 1. Estructura del repositorio (monorepo)

Sí se puede: cada nodo es un **proyecto ESP-IDF independiente** (su propio `CMakeLists.txt`,
`sdkconfig`, `build/`), y los componentes comunes viven en una carpeta compartida que cada
proyecto incluye por ruta.

```
proyecto mastergrow/
├── README.md
├── .gitignore
├── docs/                       plan, decisiones, mediciones
├── references/                 SRS, arquitecturas, contrato (pdf/docx)
├── contract/                   fuente ejecutable del contrato (Contrato §8.3)
│   ├── mqtt/topics.md
│   ├── mqtt/schemas/*.schema.json
│   ├── openapi.json            generado por FastAPI
│   └── CHANGELOG.md
├── components/                 componentes ESP-IDF COMPARTIDOS por los firmwares
│   ├── mg_wifi/                conexión STA, reconexión con backoff, eventos
│   ├── mg_mqtt/                cliente MQTT (TLS opcional por Kconfig), LWT, pub/sub con callbacks
│   ├── mg_time/                SNTP + helpers ISO-8601 UTC
│   ├── mg_settings/            lectura/escritura NVS tipada (credenciales, token, config cacheada)
│   └── mg_contract/            constantes de temas, enums y builders JSON (cJSON) del contrato
├── camera_node/                proyecto ESP-IDF — XIAO ESP32-S3 Sense
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   ├── partitions.csv
│   ├── main/
│   │   ├── app_main.c
│   │   ├── idf_component.yml   dependencias (incluye ../../components/* por path)
│   │   ├── processes/          trigger_manager, capture_task, analysis_task, upload_task,
│   │   │                       stream_task, config_sync_task, net_task, oled_task
│   │   └── config/             system_config.h, default_params.h
│   └── components/             componentes PROPIOS del nodo
│       ├── hal/                hal_camera(+ drivers ov3660/ov5640), hal_storage_sd,
│       │                       hal_light_bh1750, hal_status_led, hal_display_oled
│       └── services/           config_store, storage_queue, net_client, local_vision
├── display_node/              proyecto ESP-IDF — Waveshare ESP32-S3-Touch-LCD-4.3B-BOX
│   ├── main/                   app_main.c, tasks/, ui/ (ui_api.h, ui_bindings, ui_events, c/, lvglpro/)
│   └── components/             hal/ (lcd, touch, backlight), services/ (state_store,
│                               mqtt_subscriber, api_client, thumb_cache, command_publisher, settings_store)
└── backend/                    API FastAPI + Mosquitto + PostgreSQL (docker compose)
    ├── app/
    ├── alembic/
    ├── tests/
    ├── mosquitto/
    └── docker-compose.yml
```

**Cómo se comparten componentes** — en `camera_node/main/idf_component.yml`:

```yaml
dependencies:
  idf: ">=6.0"
  mg_wifi:      { path: ../../components/mg_wifi }
  mg_mqtt:      { path: ../../components/mg_mqtt }
  mg_time:      { path: ../../components/mg_time }
  mg_settings:  { path: ../../components/mg_settings }
  mg_contract:  { path: ../../components/mg_contract }
  espressif/esp32-camera: "*"      # fijar versión al integrar
```

Cada componente compartido declara sus propias dependencias del registry (por ejemplo
`mg_mqtt` → `espressif/mqtt`, `mg_contract` → `espressif/cjson`), así cada proyecto solo
compila lo que usa. Se prefiere esto a `EXTRA_COMPONENT_DIRS` porque deja explícito qué
usa cada nodo.

Prefijo `mg_` para evitar choques de nombre con componentes de IDF o del registry.

### Cambios respecto de IDF 5.x (el proyecto de referencia era 5.4.1)

| Antes (IDF 5.x)              | En IDF 6.1                                  |
|------------------------------|---------------------------------------------|
| `mqtt` incluido en IDF       | `espressif/mqtt` (component manager)        |
| `json` (cJSON) incluido      | `espressif/cjson`                           |
| `example_connect()`          | no usar en producto → componente `mg_wifi`  |
| `wifi_provisioning`          | `espressif/network_provisioning` (fase BLE) |

---

## 2. Qué se reutiliza del proyecto de referencia

`ESP32-IDF-framework-MQTT-Sensors` sirve como punto de partida, pero conviene **reescribir
como componentes** en vez de copiar:

| Pieza de referencia | Problema | Cómo queda |
|---|---|---|
| `mqtt_app.c` | cliente global `extern`, flag `mqtt_restart_requested` sondeado desde `app_main`, suscripciones fijas, lógica de *fallback broker* en el handler | `mg_mqtt`: handle interno, API `mg_mqtt_start(cfg)`, `mg_mqtt_publish(topic, payload, qos, retain)`, `mg_mqtt_subscribe(topic, cb)`, LWT configurable, re-suscripción automática al reconectar, eventos `MG_MQTT_EVENT_CONNECTED/DISCONNECTED` en el event loop |
| TLS con PEM embebido | — | se conserva detrás de `CONFIG_MG_MQTT_USE_TLS` (**por defecto desactivado**); sin TLS usa `mqtt://` |
| `credentials.h` ignorado por git | frágil, sin valores por defecto | menú Kconfig `MasterGrow` (SSID, password, URI del broker, URL de la API, `node_id`); los valores reales quedan en `sdkconfig` (ignorado). Más adelante, aprovisionamiento por BLE → NVS |
| `example_connect()` | es de ejemplos, bloqueante, sin reconexión controlada | `mg_wifi` con event group, backoff exponencial y RSSI para el `status` |
| `oled_display.c` (SH1106) | — | base para el `hal_display_oled` opcional del nodo cámara |

---

## 3. Decisiones transversales

- **Seguridad ahora:** TLS desactivado en MQTT/HTTP/WS por Kconfig; Mosquitto en LAN con
  usuario/contraseña desde la fase B0. Se activa TLS cuando se exponga fuera de la LAN.
- **Contrato primero:** cualquier campo nuevo se agrega en `contract/` antes de implementarlo.
  En la API, los modelos Pydantic del contrato generan los JSON Schema de MQTT; pytest valida
  los mensajes de ejemplo contra esos esquemas.
- **Concurrencia:** tareas FreeRTOS + colas que transportan **punteros/handles**, nunca copias de
  imágenes. Red en el núcleo 0; captura y análisis en el núcleo 1 (Arquitectura cámara §3.2).
- **Memoria:** framebuffers en PSRAM, número fijo de buffers. Medir con
  `uxTaskGetStackHighWaterMark` y `heap_caps_get_info`.
- **Particiones:** tabla con dos slots OTA desde el inicio (8 MB de flash en el XIAO), aunque la
  OTA se implemente al final.
- **Hora:** SNTP antes de etiquetar capturas; mientras no haya hora válida, la captura se marca
  y el `ts` se corrige o se rellena al sincronizar.
- **Logs:** `ESP_LOG*` con TAG por módulo; nada de `printf`.
- **Control de versiones:** `git init` en la raíz, un solo `.gitignore` que cubra `build/`,
  `sdkconfig`, `managed_components/`, `.venv/`, `__pycache__/`.

---

## 4. Plan por fases

Las fases del backend (B) corren en paralelo y se adelantan apenas lo justo para desbloquear al
firmware. Cada fase termina con un **criterio de "hecho"** verificable.

### Fase 0 — Base del repositorio (½ día)

- `git init`, `.gitignore` unificado, README con cómo compilar cada nodo.
- `camera_node`: `idf.py set-target esp32s3`; corregir `.vscode/settings.json` (hoy dice
  `IDF_TARGET: esp32` y OpenOCD `esp32-wrover-kit`, debe ser `esp32s3` / `esp32s3-builtin.cfg`).
- `sdkconfig.defaults` del XIAO: flash 8 MB, PSRAM **octal** 80 MHz, `CONFIG_SPIRAM_USE_MALLOC`,
  tabla de particiones propia, consola por USB-Serial-JTAG.
- `contract/`: `topics.md` y los primeros esquemas (`cam_status`, `cam_event`, `cmd`, `cmd_ack`).

**Hecho:** `idf.py build` del nodo cámara vacío para esp32s3, PSRAM detectada en el log de arranque.

### Nodo de Cámara (primero)

**C1 — Cámara funcionando (HAL)**
- Dependencia `espressif/esp32-camera`; `hal_camera.h` con `init / capture / release / deinit /
  set_resolution`, y driver `hal_camera_ov3660.c` con los pines del XIAO Sense
  (XCLK 10, SIOD 40, SIOC 39, D0–D7 = 15,17,18,16,14,12,11,48, VSYNC 38, HREF 47, PCLK 13,
  sin PWDN/RESET).
- JPEG nativo, `fb_count = 2`, `fb_location = PSRAM`, `grab_mode = LATEST`.
- `camera_type` elegido por Kconfig (`xiao-ov3660` por defecto, `xiao-ov5640` opcional).
- Herramienta de depuración (solo con `CONFIG_MG_DEBUG_HTTP`): un `esp_http_server` mínimo con
  `/capture.jpg` para ver la imagen desde el navegador, ajustar calidad y resolución y medir tamaños.
- **Riesgo:** compatibilidad de `esp32-camera` con IDF 6.1. Si falla, la alternativa es
  `esp_driver_cam` (DVP, incluido en IDF) + `espressif/esp_cam_sensor`. Detrás del HAL el resto
  del firmware no cambia.

**Hecho:** captura JPEG SVGA/UXGA estable 100 veces seguidas, tamaños y tiempos registrados,
heap sin crecer.

**C2 — Conectividad compartida** (`components/mg_*`)
- `mg_wifi`, `mg_mqtt` (sin TLS), `mg_time` (SNTP), `mg_settings` (NVS), `mg_contract` (temas
  `growlab/cam/{id}/…` y JSON con `schema_version`, `node_id`, `ts`).
- `net_task`: publica `status` retenido QoS 1 al conectar y en cada cambio; LWT con
  `estado_nodo: fuera_de_linea`; se suscribe a `cmd`, responde `cmd/ack`, descarta `cmd_id`
  repetidos y comandos vencidos (ventana de 60 s).
- Requiere **B0** (Mosquitto local).

**Hecho:** `mosquitto_sub -t 'growlab/#' -v` muestra el `status`; al desenchufar el XIAO aparece
el LWT; `capture_now` publicado a mano recibe `accepted`.

**C3 — Pipeline y gestor de disparos**
- `trigger_manager` con `q_trigger`: orígenes `timer` (intervalo configurable, 120 min por defecto)
  y `comando_remoto`; validación de la ventana de luz; anti-duplicado por intervalo mínimo;
  prioridad alta y resolución completa para evento o post-actuación.
- `capture_task` → `q_frame` → `analysis_task` (vacía por ahora) → `q_result`.
- Struct `capture_job_t` con metadatos del contrato (`trigger_type`, `resolucion`, `prioridad`,
  `ts`, `sensores_snapshot` con `null` donde falte dato).

**Hecho:** captura por timer y por `capture_now` en menos de 5 s, con su `trigger_type` correcto.

**C4 — Store-and-forward en SD**
- `hal_storage_sd` (SPI, montado con FATFS). **Ojo:** en el XIAO Sense el CS de la SD es GPIO21,
  el mismo pin que el LED de usuario, así que `hal_status_led` no puede usarlo con la SD activa.
- `storage_queue`: guarda `cap_<ts>.jpg` + `cap_<ts>.json`, estado `pendiente`, índice al arrancar;
  política de limpieza (rutinarias más viejas primero, conservar las de prioridad; evento `sd_llena`
  al 80 %); `sd_libre_pct` en el `status`.

**Hecho:** sin red se acumulan capturas en la SD y sobreviven a un reinicio.

**C5 — Subida HTTP** (requiere **B1**)
- `upload_task` en el núcleo 0: `POST /api/v1/capturas` multipart (JPEG + metadatos JSON), token
  en `Authorization`, reintentos con backoff, borrado de la SD solo tras el 201.

**Hecho:** se corta el WiFi, se acumulan 5 capturas, vuelve la red y las 5 llegan a la API.

**C6 — Configuración remota** (requiere **B3**)
- `config_sync_task`: `GET /config/nodos/{id}` al arrancar, al recibir `refresh_config` y de forma
  periódica; guarda en NVS la config efectiva con su versión; si no hay red, usa la caché
  (modo `autonomo`).
- Máquina de estados `arrancando → operativo ↔ autonomo → error`.

**C7 — Análisis local HSV**
- `local_vision/hsv.c`: decodificar el JPEG a RGB565 escalado (1/4 u 1/8), calcular la cobertura
  de verde, la proporción de amarillo/marrón, el brillo medio y la detección de escena oscura.
- `analisis_local` en los metadatos; evento `anomalia_planta` o `luz_apagada_anticipada` con
  severidad; subida prioritaria si supera `umbral_sospecha_hsv`.
- El modelo TFLite Micro (`analysis_mode: hybrid/model`) queda para después, cuando haya dataset propio.

**C8 — Vista en vivo** (requiere **B5**)
- `stream_task` con `espressif/esp_websocket_client`: al recibir `start_stream` cambia el sensor a
  QVGA/VGA, envía JPEG binarios respetando `fps_max` y `ttl_s`, difiere los disparos por timer y,
  ante un `capture_now`, pausa → captura completa → retoma.

**Hecho:** primer cuadro en menos de 3 s, al menos 5 fps a QVGA en la LAN.

**C9 — Extras:** ~~BH1750~~ (pasa al módulo de control: la cámara tomaría `lux` de ahí), OLED
opcional, aprovisionamiento BLE, OTA, activar TLS, prueba continua de 24 h.

### Backend FastAPI (en paralelo)

Stack: Python 3.12, FastAPI, SQLAlchemy 2 (async) + Alembic, PostgreSQL, `aiomqtt`,
`pydantic-settings`, Pillow (miniaturas), pytest + httpx. Todo en `docker compose`
(api + mosquitto + postgres), configurado por variables de entorno (listo para Railway o AWS).

| Fase | Contenido | Desbloquea |
|---|---|---|
| **B0** | compose con Mosquitto (usuario/contraseña, sin TLS) + Postgres + esqueleto FastAPI `/api/v1/health`; estructura `app/{api,models,schemas,services,storage}` | C2 |
| **B1** | `POST/GET /capturas`, `/capturas/{id}/imagen`, `/thumb`; interfaz `ImageStore` (disco local, S3 después); token por nodo | C5, D3 |
| **B2** | ingestor MQTT: `growlab/+/+/status` → tabla `nodos`, `event` → tabla `eventos`; `GET/PATCH /eventos` | D2, D4 |
| **B3** | `config_global` + `config_nodo` con resolución override > global > default y versión | C6, D4 |
| **B4** | `/nodos` (alta con emisión de token), `/zonas` | D4 |
| **B5** | `/stream/sesiones` + WebSockets `publish`/`view`, reenvío del último cuadro y descarte para espectadores lentos | C8, D5 |
| **B6** | exportar `openapi.json` y los JSON Schema a `contract/`, tests de contrato | — |

Con B0 + B1 (y una pequeña página HTML con las últimas capturas) ya se puede validar la cámara de
punta a punta antes de tener la pantalla.

### Nodo de Pantalla (después de C5)

**D0 — Plataforma:** proyecto `display_node` para esp32s3 (16 MB flash, 8 MB PSRAM octal),
panel RGB 800×480 con `esp_lcd` RGB y bounce buffers, táctil GT911, expansor de E/S CH422G
(backlight/reset), LVGL 9 con `espressif/esp_lvgl_port`. Verificar si Waveshare publica un BSP
para la 4.3B y usarlo como base del HAL.
**Hecho:** pantalla "hola" con un botón táctil, sin tearing.

**D1 — Datos:** reutilizar `mg_wifi`, `mg_mqtt`, `mg_time`, `mg_contract`; `state_store` alimentado
por `growlab/cam/+/status|event` y `growlab/ctrl/+/state`; cola hacia `lvgl_task` (la única tarea
que toca LVGL); marca de "dato antiguo".

**D2 — Inicio y Cámaras (variante C):** `theme.c`, componentes (tarjeta de KPI, tarjeta de cámara,
barra de estado, barra de navegación), botón "Capturar ahora" con `cmd_id`, espera de ack y timeout.

**D3 — Miniaturas:** `api_client` + `thumb_cache` (RAM + tarjeta TF), decodificación JPEG por software.

**D4 — Eventos, Detalle de cámara, Configuración** (override, PIN opcional).

**D5 — Vista en vivo:** `video_task` WebSocket, descarte de cuadros atrasados, guías de encuadre,
medición de fps.

**D6 — Variante LVGL Pro** de Inicio y Cámaras para comparar con la C; NTP + RTC; OTA.
Ambiente, Cultivo y Actuadores esperan al contrato del módulo de control (Contrato §4.4).

---

## 5. Orden sugerido e hitos

```
Semana 1   F0 · C1 · B0
Semana 2   C2 · C3 · B1            → Hito M1: captura por comando MQTT visible en mosquitto_sub
Semana 3   C4 · C5 · B2 · B3       → Hito M2: captura sube a la API y se ve en un HTML simple
Semana 4   C6 · C7 · D0
Semana 5   D1 · D2 · D3 · B4       → Hito M3: la pantalla muestra la tarjeta y la miniatura de la cámara
Semana 6+  B5 · C8 · D5 · D4 · D6 · C9
```

---

## 6. Inconsistencias detectadas en los documentos (resolver en el Contrato)

| Tema | SRS Cámara | Contrato (se adopta) |
|---|---|---|
| severidad | info / advertencia / crítico | baja / media / alta |
| trigger_type | rutina / evento / manual / post_actuación / calendario | timer / evento_sensor / comando_remoto / post_actuacion / calendario |
| estado_evento | activo / resuelto | abierto / reconocido / resuelto |
| tipo_evento | luz_apagada_temprano, sensor_fuera_rango | luz_apagada_anticipada, … (falta sensor_fuera_rango) |
| OTA | "no hay OTA en esta versión" (§2.5) | la Arquitectura la incluye → se deja la tabla de particiones preparada |
| Seguridad | comunicaciones cifradas obligatorias | TLS desactivado en desarrollo (decisión actual), activable por Kconfig |

Hardware:
- **XIAO Sense:** GPIO21 lo comparten el CS de la SD y el LED de usuario (`hal_status_led`).
- Los nombres de campo del Contrato .docx mezclan español e inglés (`camera_type`, `estado_nodo`).
  Desde el 2026-11-18 el contrato ejecutable usa solo inglés: equivalencia en `contract/NAMING.md`.

---

## 7. Próximo paso concreto

Fase 0 + C1: estructurar el repo, `set-target esp32s3`, `sdkconfig.defaults` del XIAO, y
`hal_camera` con OV3660 + endpoint de depuración `/capture.jpg`.
