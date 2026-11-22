# MasterGrow — contexto del proyecto

Sistema IoT de automatización de cultivo indoor. Monorepo con 3 módulos independientes que se
comunican **solo** por broker MQTT y API HTTP (nunca entre sí directamente):

- **Nodo de cámara** — Seeed XIAO ESP32-S3 Sense, OV3660 (8 MB flash, 8 MB PSRAM octal). ESP-IDF.
- **Nodo de pantalla** — Waveshare ESP32-S3-Touch-LCD-4.3B-BOX (800×480, LVGL). ESP-IDF. Pendiente.
- **Módulo de control y sensores** — fuera de alcance por ahora.
- **Backend** — API propia FastAPI + Mosquitto + PostgreSQL (docker compose). Pendiente.

Idioma de trabajo: español para la conversación, comentarios, logs, textos de UI y documentos (plan, SRS).
**Todo lo que es código va en inglés**: identificadores, nombres de archivos y carpetas, tablas y columnas,
y también el contrato que viaja por la red (campos JSON, valores de enums, rutas HTTP, códigos de error).
El Contrato .docx de referencia usa nombres en español: equivalencia en `contract/NAMING.md`.

## Documentos de referencia

- `references/pdf|docs/`: SRS (IEEE 830) de cámara, pantalla y control; arquitectura de cámara y
  pantalla; **Contrato de interfaces** (referencia única de temas MQTT, endpoints y modelo de datos).
- Si el SRS y el Contrato difieren, **prevalece el Contrato** (ver `docs/plan-desarrollo.md` §6).
- Plan por fases: `docs/plan-desarrollo.md`. Fuente ejecutable del contrato: `contract/`.

## Entorno y comandos

- ESP-IDF **v6.1** en `~/.espressif/v6.1/esp-idf`. Activar en cada shell:
  `source ~/.espressif/tools/activate_idf_v6.1.sh`
- El XIAO aparece como `/dev/cu.usbmodem101`.
- Backend: `cd backend && docker compose up -d --build` (Docker vía OrbStack: `~/.orbstack/bin/docker`).
  Claves en `backend/.env` (gitignored, generadas al azar). Tests: `uv run pytest`, lint: `uv run ruff check .`
- Ver tráfico MQTT: `backend/scripts/mqtt_watch.sh`; validarlo contra los esquemas:
  `cd backend && uv run python scripts/mqtt_validate.py 60`.
- Si cambia la API: `uv run python scripts/export_contract.py` (regenera `contract/openapi.json`). Enviar comando: `backend/scripts/cam_cmd.sh capture_now`.
- Capturas subidas: `http://localhost:8000/gallery?token=<token dev de API_USER_TOKENS en backend/.env>`.
- La red del usuario NO responde NTP de pool.ntp.org por IPv4; time.google.com sí (es el primario).
- **Nunca `uv run --active` con el entorno de IDF activado**: instala las dependencias del backend
  en el venv de IDF y rompe `idf.py` (pasó con `click`). Reparar con
  `python -m pip install -r $IDF_PATH/tools/requirements/requirements.core.txt -c ~/.espressif/tools/espidf.constraints.v6.1.txt`.
- El sandbox de Claude bloquea UDP desde Python: no sirve para probar NTP/DNS; usar `sntp` del sistema.
- Compilar y grabar la cámara:
  `cd camera_node && idf.py -p /dev/cu.usbmodem101 build flash monitor`
- **La IP de la Mac cambia por DHCP** (.182 → .169 el 2026-11-13 → .189 el 2026-11-22): si la cámara queda
  `offline`, comparar `ipconfig getifaddr en0` con CONFIG_MG_MQTT_BROKER_URI. Pendiente decidir:
  reserva DHCP en el router o mDNS (`<mac>.local` con espressif/mdns).
- El scratchpad de Claude se borra entre sesiones: `readlog.py` (lector serie con reset) hay que recrearlo.
- Credenciales WiFi y MQTT (broker = IP de la Mac) en `camera_node/sdkconfig.defaults.local`
  (gitignored). Tras editarlo: `rm sdkconfig`. Si el usuario tiene un `idf.py monitor` abierto, el puerto
  queda ocupado: pedirle que lo cierre con Ctrl+] antes de grabar.
- Pantalla: `cd display_node && idf.py -p /dev/cu.usbmodem1101 build flash` (USB nativo; si están
  las dos placas, la pantalla es la de 16 MB de flash). **Nunca dejarla con `esptool --after
  no-reset`**: queda marcado "forzar descarga" y cada reset por RTS cae en el bootloader
  (`boot:0x0 … waiting for download`). Se sale con
  `esptool --port <p> --before no-reset --after watchdog-reset chip-id` o con el botón RESET.
  Para leer el arranque usar un lector que reabra el puerto (el USB se re-enumera en cada reset).
- La extensión ESP-IDF de VS Code solo funciona abriendo la carpeta del nodo (`camera_node`), no la raíz.
- En IDF 6.x, **mqtt, cjson y wifi_provisioning ya no vienen en IDF**: usar `espressif/mqtt`,
  `espressif/cjson`, `espressif/network_provisioning` del component registry.

## Convenciones de código

- Componentes compartidos en `components/` con prefijo `mg_`; cada proyecto los referencia por
  `path: ../../components/<nombre>` en `main/idf_component.yml`.
- HAL del nodo cámara = componente `node_hal` (no `hal`: choca con el componente interno de IDF).
  Un driver por `camera_type` implementando `hal_camera_driver_t` (`priv_include/hal_camera_driver.h`).
- Componentes devuelven `esp_err_t` (`ESP_RETURN_ON_ERROR`), no abortan con `ESP_ERROR_CHECK`.
- Opciones bool de Kconfig: usar `#if CONFIG_X`, nunca `CONFIG_X ? :` (si es `n` la macro no existe).
- Tareas FreeRTOS + colas con punteros (nunca copias de imágenes). Red en núcleo 0, captura/análisis en núcleo 1.
- Mensajes MQTT con `schema_version`, `node_id`, `ts` (ISO-8601 UTC). Temas `growlab/{cam|ctrl|disp}/{id}/{canal}`.
- TLS **desactivado por ahora** (decisión del usuario), activable por Kconfig más adelante.
- Cambios de contrato: primero en `contract/` + `CHANGELOG.md`, después en los módulos.
- Hardware: en el XIAO Sense, GPIO21 = CS de la microSD **y** LED de usuario (conflicto).

## Estado actual (actualizar al cerrar cada fase)

- [x] **Fase 0** — monorepo, git, `contract/` v1 (schemas status/event/cmd/cmd_ack), sdkconfig del XIAO, particiones OTA.
- [x] **C1** — `node_hal` + OV3660 funcionando en la placa; `mg_wifi`; servidor de depuración
      (`/`, `/capture.jpg`, `/status`, `/control`). Medido: SVGA ≈35 ms / 14,5 KB; UXGA ≈39 ms / 57 KB.
- [x] **B0** — docker compose: Mosquitto 2.0 (usuarios = node_id, ACL por tema según Contrato §8.2, sin TLS)
      + Postgres 16 + FastAPI con `/api/v1/health`. Probado.
- [x] **C2** — `mg_mqtt`, `mg_time` (3 servidores NTP + evento MG_TIME_EVENT_SYNCED), `mg_contract` (cJSON);
      `net_task`: status retenido + LWT, cmd → ack accepted/done/rejected, cmd_id repetidos, vigencia 60 s.
      Probado en placa salvo el arreglo de NTP (grabar y confirmar que `ts` ya no sale en 1970).
      `mg_settings` (NVS) se pospone a C6. Handlers actuales: capture_now (captura directa) y reboot.
- [x] **C3** — pipeline en `main/processes/`: trigger_manager (timer en ventana de luz con offset UTC,
      prioridad alta → UXGA al frente de la cola, anti-duplicado 5 s) → capture_task (copia JPEG a PSRAM y
      libera el fb) → q_frame → analysis_task (pasa de largo hasta C7) → pipeline_job_done (log + free; en C4 → SD).
      capture_now es asíncrono: el ack "done" lo manda capture_task. net_task llama al handler ANTES de
      responder, así un rechazo nunca va precedido de "accepted". Probado en placa.
      (El intervalo de pruebas de 1 min ahora va como override en la API, ver C6.)
- [x] **C4** — `node_hal/hal_storage` (SDSPI + FATFS en /sd, nunca formatea, pull-ups internos + 3 reintentos)
      y servicio `components/services/storage_queue` (agregado con EXTRA_COMPONENT_DIRS): <id>.jpg + <id>.json en
      /sd/mg/pend, escritura atómica (temporales + rename), índice en PSRAM reconstruido al arrancar,
      limpieza (rutinarias viejas primero; máx 2000 pendientes; mín 10 % libre), evento sd_full al 80 %
      con histéresis. capture_task guarda ANTES de q_frame; metadatos del Contrato §5.2 en pipeline_meta_json.
      Debug: GET /storage y /storage/img?id=. Probado con microSD de 16 GB y sin tarjeta. Las capturas
      tomadas antes de sincronizar NTP quedan con id `19700101T000000Z_...` (tenerlo en cuenta en C5).
- [x] **B1** — `POST/GET /captures`, `/{id}`, `/image`, `/thumb` (320×240 con Pillow, cacheada) según
      `contract/http/captures.md`; Alembic (migra al arrancar el contenedor); `ImageStore` local en el
      volumen `images`; tokens por env `API_NODE_TOKENS` / `API_USER_TOKENS` (B4 los pasa a la base);
      `Idempotency-Key` = id de la SD (reintento → 200 sin duplicar); `timestamp: null` → hora de
      recepción + `timestamp_approx`. Galería: `http://<mac>:8000/gallery?token=<token de usuario>`.
      12 tests (SQLite en memoria) incluido el chequeo de enums contra `contract/`.
- [x] **C5** — componente compartido `mg_api` (POST multipart en streaming, Bearer, Kconfig
      `MG_API_BASE_URL`/`MG_API_TOKEN`) + `upload_task` (núcleo 0, prio 4): sube de la SD (alta y más vieja
      primero; `storage_queue` marca `in_progress` hasta que termina el pipeline), borra solo tras 201/200,
      descarta con 400/413/415/422, backoff 2→300 s con ±20 % (401/403 → 300 s). Sin SD sube desde RAM
      (3 intentos). Probado: API detenida → 5 pendientes → al volver suben las 5. `/storage` muestra stats.
      La URL de la API también usa la IP de la Mac (mismo problema de DHCP que el broker).
- [x] **B2** — ingestor MQTT (`app/mqtt/ingestor.py`, aiomqtt, sesión persistente `mastergrow-api`,
      tarea de fondo en el lifespan; lógica sin broker en `services/ingest.py`): `status`/`state` → tabla
      `nodes`, `event` → tabla `events` (dedupe por node_id+event_type+ts; ts < 2020 → hora de recepción).
      La API abre `node_disconnected` al llegar el LWT y lo resuelve cuando el nodo vuelve.
      `GET/PATCH /events`, `GET /nodes` (solo lectura hasta B4), `/captures?zone_id=` ya filtra,
      `/health` informa `mqtt`. 28 tests. Probado con la placa y con un nodo sintético (evento
      publicado con la API caída llega al volver). Contrato: `contract/http/events.md` y `nodes.md`.
- [x] **B3** — configuración override → global → default (`schemas/config.py` `CamParams` = fuente de
      parámetros, defaults iguales a Kconfig del firmware, lo verifica un test). Tablas `config_global`,
      `config_node`, `config_revisions` (versión = última revisión global o del nodo, entero creciente).
      `GET/PUT /config/global`, `GET /config/nodes/{id}` (ETag → 304; un nodo solo lee la suya),
      `GET/PUT/DELETE .../override` (PUT parcial, `null` borra). Tras cada cambio publica `refresh_config`
      con `params.version` (hoy la placa lo rechaza `unsupported_action`: lo implementa C6). 38 tests.
- [x] **C6** — servicio `config_store` (components/services: config efectiva + versión en NVS, valida cada
      campo con los rangos del contrato; inválido → default del firmware) y `config_sync_task` (núcleo 0,
      prio 3): GET `/config/nodes/{id}` con If-None-Match al arrancar, con `refresh_config` y cada
      `CONFIG_MG_CONFIG_SYNC_PERIOD_S` (900 s); backoff 10 s→período. Aplica a trigger_manager y a la
      calidad JPEG (la cambia capture_task, nunca otra tarea). Status publica `config_version`. Debug:
      `GET /config`. Probado: cambio en vivo, 304 tras reboot (NVS), arranque con la API caída.
      **Ahora la config remota manda**: `CONFIG_MG_CAPTURE_INTERVAL_MIN` es solo el default; para pruebas
      `cam-01` tiene un override `capture_interval_min: 1` en la API.
- [x] **C7** — `components/services/local_vision` (hsv.c): `espressif/esp_jpeg` (tjpgd de la ROM, con escala)
      decodifica a ~200 px de ancho en PSRAM; clasifica píxeles verde / amarillo-marrón / oscuro →
      `local_analysis` en los metadatos (SD y API). Puntaje = amarillo/marrón dentro de la planta (0 si está
      oscuro o hay < 5 % de planta). `plant_anomaly` al pasar el umbral (`hsv_suspicion_threshold`, rearme
      al 80 %), `early_lights_off` si está oscuro dentro de la ventana de luz; una vez por episodio.
      Sospechosa o con evento → `storage_queue_promote` (`_n_` → `_h_`, sube primero). `hybrid`/`model`
      se comportan como `hsv` hasta que exista el modelo. Medido: 200×150 en ~340 ms desde SVGA.
      Umbrales de color sin calibrar: hace falta un set de imágenes propias (sana / clorosis / seca).
- [x] **B4** — tabla `zones` (`grow_mode` substrate/coco_coir/dwc, `light_cycle` {start,end}) y
      `node_tokens` (solo SHA-256, revocable). `POST /nodes` da de alta y entrega el token UNA vez;
      `POST/DELETE /nodes/{id}/token` rota/revoca; `PUT /nodes/{id}/zone`; `GET/POST /zones`,
      `GET/PATCH/DELETE /zones/{id}` (409 si tiene nodos). Config: override → **zona** → global →
      default (solo `light_window`); mover de zona o cambiar `light_cycle` crea versión y publica
      `refresh_config`. `API_NODE_TOKENS` se importa a la base al arrancar (si nunca se vio y el
      nodo no tiene otro vigente). Tokens de usuario siguen en `.env`. 48 tests. Probado contra
      Postgres: `cam-01` en `zone-a` (06:00–00:00, igual que el default), config v7.
- [x] **B5** — vista en vivo en la API (`app/stream/hub.py`, en memoria, un solo worker):
      `POST/GET /stream/sessions`, `GET/DELETE /stream/sessions/{id}`, WS `/stream/{id}/publish`
      (token del NODO, sesión de ese nodo) y `/stream/{id}/view` (token de la sesión, `?token=`).
      Una sesión por cámara (otro pedido se suma: 200); parámetros recortados a la config del nodo;
      `start_stream` cmd_id `st-{sid}` con `ws_publish_url` RELATIVA salvo `API_PUBLIC_URL`;
      espectador lento → solo el último cuadro. Fin: DELETE, ttl, 30 s sin espectadores, 15 s sin
      publicador, ack `rejected` (el ingestor ahora escucha `cmd/ack`), publicador cierra. Tabla
      `stream_sessions`. Página `/live?token=&node=cam-01`. 60 tests (WS con uvicorn real en el
      loop del test). Probado en Docker con nodo sintético: 50/50 cuadros a 9,6 fps.
- [x] **C8** — `main/processes/stream_task` (núcleo 1, prio 5) con `espressif/esp_websocket_client`
      1.8: start_stream → WS de publicación (token del nodo; `mg_api_ws_url` resuelve la ruta
      relativa contra MG_API_BASE_URL), cuadros a fps_max hasta stop_stream / ttl / cierre de la API;
      límites de la config local además de los de la API. Ack `done` al salir el primer cuadro,
      `rejected` + `stream_connect_failed`. Candado de cámara (`capture_task_camera_lock`): un
      capture_now en pleno stream sale en UXGA y el stream retoma; el timer se difiere y sale al
      terminar. Medido en la placa: primer cuadro 163–209 ms; QVGA 10,0 fps (~6 KB), VGA 10,0 fps
      (13 KB), 0 descartes.
- [x] **B6** — `contract/openapi.json` (export estable con `scripts/export_contract.py`, test que
      falla si queda viejo); `tests/test_contract.py` (esquemas válidos + ejemplos, comandos de la
      API vs `cmd.schema.json`, enums de la API = esquemas); `scripts/mqtt_validate.py` valida el
      tráfico real (probado con la placa: status y cmd/ack OK). `jsonschema` + `rfc3339-validator`
      (sin este último `date-time` no se valida). 79 tests.
- [ ] C9 extras (OTA, aprovisionamiento BLE, TLS, prueba de 24 h). El BH1750 (lux) pasa al
      módulo de control; la cámara tomaría `lux` de ahí para `sensor_snapshot`.
- [x] **D0** — `display_node` (Waveshare ESP32-S3-Touch-LCD-4.3B, N16R8). `node_hal`: `hal_board`
      (I2C nuevo `i2c_master` en GPIO8/9 + CH422G: comando 0x24 = config, 0x38 = salidas EXIO;
      EXIO1 reset táctil, EXIO2 luz, EXIO3 reset LCD, EXIO4 CS SD), `hal_lcd` (RGB565 16 MHz,
      sincronismos 4/8/8, pclk flanco negativo, bounce 10 líneas, 2 fb en PSRAM; en IDF 6.1
      `in/out_color_format` en vez de `bits_per_pixel`), `hal_touch` (GT911 0x5D: INT bajo
      durante el reset). LVGL **9.3** fija (9.6 rompe con -Werror) + `esp_lvgl_port` 2.9 en el
      núcleo 1, `avoid_tearing` + `direct_mode`. `SPIRAM_XIP_FROM_PSRAM`; sin
      `LV_ATTRIBUTE_FAST_MEM_USE_IRAM` (choca con GCC 15). Arranque 0,84 s; libre 266 KB
      internos / 6,1 MB PSRAM. Medido: 38–45 fps (tope = refresco del panel a 16 MHz), sin tearing
      (barra animada entera); CPU 86–94 % con animación continua (incluye la espera del vsync). Datos de la placa sacados del ejemplo oficial 09_lvgl_v9_demo.
- [x] **D1** — red (núcleo 0): `mg_wifi`/`mg_time`/`mg_mqtt` compartidos, usuario MQTT `disp-01`,
      status propio retenido + LWT en `growlab/disp/disp-01/status` (la API lo registra como nodo
      `disp`). Callback MQTT → cola → `net_task` → `model/state_store` (nodos ordenados + anillo de
      32 eventos, snapshots, observadores). "Dato antiguo" por el `ts` del status si hay hora (sirve
      con retenidos viejos), `CONFIG_MG_STALE_AFTER_S` = 660. `ui/ui_bridge`: observador → cola →
      `lv_timer` (dentro de la tarea de LVGL) cada 100 ms + tick de 1 s. Pantalla `ui_status`
      (textos sin tildes hasta tener fuentes Latin-1). Credenciales en
      `display_node/sdkconfig.defaults.local` (WiFi/broker/API de la cámara, MQTT de disp-01, token
      de usuario `pantalla`). Probado: LWT offline, cámara que se conecta en vivo, evento en la lista.
- [x] **D2** — interfaz variante C (`ui/ui_api.h` = única interfaz; `ui/c/`): `theme` (paleta de la
      maqueta Fig. 3, grilla 8 px), fuentes **TinyTTF** con `fonts/Montserrat-Medium.ttf` embebida
      (tildes; `npx lv_font_conv` está bloqueado por el sandbox) y respaldo a las Montserrat de LVGL
      para `LV_SYMBOL_*`. `ui_shell`: barra superior (WiFi, nodos en línea, alarmas, hora) y
      navegación inferior (7 pantallas + voz deshabilitada); Inicio (KPI "--" hasta el módulo de
      control, gráfico vacío, última captura), Cámaras (tarjeta por nodo + últimos eventos),
      Eventos (lista), resto "en construcción". `services/command_publisher`: capture_now con
      cmd_id `disp-01-<n>-<azar>`, acks por `growlab/cam/+/cmd/ack`, timeout 5 s accepted / 20 s
      done; requiere hora válida. Probado en la placa: captura c-000211 `remote_command` UXGA.
      RAM interna libre 138 KB (cachés de glifos).
- [ ] D3–D6 nodo de pantalla.
