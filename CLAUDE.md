# MasterGrow — contexto del proyecto

Sistema IoT de automatización de cultivo indoor. Monorepo con 3 módulos independientes que se
comunican **solo** por broker MQTT y API HTTP (nunca entre sí directamente):

- **Nodo de cámara** — Seeed XIAO ESP32-S3 Sense, OV3660 (8 MB flash, 8 MB PSRAM octal). ESP-IDF.
- **Nodo de pantalla** — Waveshare ESP32-S3-Touch-LCD-4.3B-BOX (800×480, LVGL). ESP-IDF. Pendiente.
- **Módulo de control y sensores** — fuera de alcance por ahora.
- **Backend** — API propia FastAPI + Mosquitto + PostgreSQL (docker compose). Pendiente.

Idioma de trabajo: español (código con identificadores en inglés; comentarios y logs en español).

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
- Ver tráfico MQTT: `backend/scripts/mqtt_ver.sh`. Enviar comando: `backend/scripts/cam_cmd.sh capture_now`.
- La red del usuario NO responde NTP de pool.ntp.org por IPv4; time.google.com sí (es el primario).
- El sandbox de Claude bloquea UDP desde Python: no sirve para probar NTP/DNS; usar `sntp` del sistema.
- Compilar y grabar la cámara:
  `cd nodo_camara && idf.py -p /dev/cu.usbmodem101 build flash monitor`
- **La IP de la Mac cambia por DHCP** (era .182, el 2026-11-13 pasó a .169): si la cámara queda
  `fuera_de_linea`, comparar `ipconfig getifaddr en0` con CONFIG_MG_MQTT_BROKER_URI. Pendiente decidir:
  reserva DHCP en el router o mDNS (`<mac>.local` con espressif/mdns).
- El scratchpad de Claude se borra entre sesiones: `readlog.py` (lector serie con reset) hay que recrearlo.
- Credenciales WiFi y MQTT (broker = IP de la Mac) en `nodo_camara/sdkconfig.defaults.local`
  (gitignored). Tras editarlo: `rm sdkconfig`. Si el usuario tiene un `idf.py monitor` abierto, el puerto
  queda ocupado: pedirle que lo cierre con Ctrl+] antes de grabar.
- La extensión ESP-IDF de VS Code solo funciona abriendo la carpeta del nodo (`nodo_camara`), no la raíz.
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
      `sdkconfig.defaults.local` del usuario tiene CONFIG_MG_CAPTURE_INTERVAL_MIN=1 para pruebas.
- [~] **C4** — `node_hal/hal_storage` (SDSPI + FATFS en /sd, nunca formatea) y servicio
      `components/services/storage_queue` (agregado con EXTRA_COMPONENT_DIRS): <id>.jpg + <id>.json en
      /sd/mg/pend, escritura atómica (temporales + rename), índice en PSRAM reconstruido al arrancar,
      limpieza (rutinarias viejas primero; máx 2000 pendientes; mín 10 % libre), evento sd_llena al 80 %
      con histéresis. capture_task guarda ANTES de q_frame; metadatos del Contrato §5.2 en pipeline_meta_json.
      Debug: GET /storage y /storage/img?id=. **Probado SOLO sin tarjeta** (sigue operando, sd_libre_pct null).
      Falta probar con microSD FAT32 colocada: guardar, reiniciar, verificar índice y /storage.
- [ ] **C5** — upload_task + B1 (POST /capturas) · C6 config remota ·
      C7 análisis HSV · C8 vista en vivo · C9 extras. Luego nodo de pantalla D0–D6.
