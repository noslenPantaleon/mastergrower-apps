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
- Compilar y grabar la cámara:
  `cd nodo_camara && idf.py -p /dev/cu.usbmodem101 build flash monitor`
- Credenciales WiFi en `nodo_camara/sdkconfig.defaults.local` (gitignored). Tras editarlo: `rm sdkconfig`.
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
- [ ] **B0** — docker compose: Mosquitto (usuario/clave, sin TLS) + Postgres + esqueleto FastAPI `/api/v1/health`.
- [ ] **C2** — `mg_mqtt` (sin TLS), `mg_time` (SNTP), `mg_settings` (NVS), `mg_contract` (cJSON);
      `net_task`: status retenido + LWT, suscripción a `cmd`, `cmd/ack`, descarte de `cmd_id` repetidos.
- [ ] C3 pipeline/trigger_manager · C4 SD store-and-forward · C5 subida HTTP · C6 config remota ·
      C7 análisis HSV · C8 vista en vivo · C9 extras. Luego nodo de pantalla D0–D6.
