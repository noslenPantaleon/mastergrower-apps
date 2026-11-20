# MasterGrow — Sistema de cultivo indoor

Monorepo con los módulos del sistema. Plan completo en [docs/plan-desarrollo.md](docs/plan-desarrollo.md);
documentos de especificación en [references/](references/).

| Carpeta | Qué es | Estado |
|---|---|---|
| [contract/](contract/) | Fuente ejecutable del contrato (temas MQTT y JSON Schema) | v1 inicial |
| [components/](components/) | Componentes ESP-IDF compartidos (`mg_wifi`, `mg_mqtt`, `mg_time`, `mg_contract`) | en desarrollo |
| [camera_node/](camera_node/) | Firmware del nodo de cámara — Seeed XIAO ESP32-S3 Sense (OV3660) | fase C2 |
| `display_node/` | Firmware del dashboard — Waveshare ESP32-S3-Touch-LCD-4.3B-BOX | pendiente |
| [backend/](backend/) | API FastAPI + Mosquitto + PostgreSQL (docker compose) | fase B0 |

## Requisitos

- ESP-IDF **v6.1** (instalado con EIM en `~/.espressif/v6.1/esp-idf`).
- Activar el entorno en cada terminal:
  ```sh
  source ~/.espressif/tools/activate_idf_v6.1.sh
  ```

## Backend (broker, base de datos y API)

```sh
cd backend
cp .env.example .env            # y cambiar las claves
docker compose up -d --build    # mosquitto :1883 (LAN), postgres :5432 (local), api :8000
curl http://localhost:8000/api/v1/health
```

- `./scripts/mqtt_watch.sh` muestra todo el tráfico MQTT.
- `./scripts/cam_cmd.sh capture_now [cam-01]` envía un comando y muestra el `cmd/ack`.
- Estado de los nodos y eventos (los guarda el ingestor MQTT de la API):
  `GET /api/v1/nodes`, `GET /api/v1/events`, `PATCH /api/v1/events/{id}` (ver `contract/http/`).
- Capturas subidas por los nodos: `http://localhost:8000/gallery?token=<token de usuario>`
  (`API_USER_TOKENS` en `.env`). Documentación interactiva: `http://localhost:8000/docs`.
- Tests y lint: `uv run pytest` · `uv run ruff check .`. Migraciones: `uv run alembic revision --autogenerate -m "..."`
  (se aplican solas al arrancar el contenedor).
- Desarrollo de la API: `uv sync`, `uv run pytest`, `uv run ruff check .`

## Nodo de cámara

```sh
cd camera_node
cp sdkconfig.defaults.local.example sdkconfig.defaults.local   # WiFi + broker MQTT + clave de cam-01
rm -f sdkconfig                                                 # solo si ya existía, para tomar los defaults
idf.py set-target esp32s3                                       # una vez
idf.py build flash monitor
```

Con `CONFIG_MG_DEBUG_HTTP=y` (por defecto en desarrollo), el log muestra
`Vista previa: http://<ip>/`. Desde el navegador se pueden capturar imágenes y probar la
resolución, la calidad JPEG y la orientación.
Endpoints: `/capture.jpg`, `/status`, `/control?res=SVGA&quality=12&vflip=1&hmirror=0`.

Opciones propias en `idf.py menuconfig`:
- **MasterGrow - Nodo de cámara**: `node_id`, resolución y calidad, autoprueba, servidor de depuración.
- **MasterGrow - Cámara (HAL)**: sensor (`xiao-ov3660` / `xiao-ov5640`), XCLK, framebuffers, orientación.
- **MasterGrow - WiFi**: SSID, contraseña, backoff de reconexión.

## Convenciones

- Componentes compartidos con prefijo `mg_`; cada proyecto los declara por `path` en
  `main/idf_component.yml`.
- El HAL del nodo se llama `node_hal` (IDF ya tiene un componente `hal`).
- Credenciales fuera de git: `sdkconfig` y `sdkconfig.defaults.local` están en `.gitignore`.
- `dependencies.lock` **sí** se versiona para builds reproducibles.
- Cambios de mensajes o endpoints: primero en `contract/` (con entrada en `CHANGELOG.md`),
  después en los módulos.
