# Guía de uso paso a paso (demostración del sistema)

Guía para recorrer el flujo completo de MasterGrow tal como está hoy, pensada para seguirla
mientras se graba un video. Cada paso dice **qué hacer**, **qué mostrar** y **qué se espera ver**.
Duración total aproximada: 25 minutos.

## Qué se va a mostrar

```
 Cámara XIAO ESP32-S3 ──MQTT (estado, eventos)──► Mosquitto ◄──MQTT── Pantalla Waveshare 4.3B
        │                                            │                       │
        └──HTTP (capturas, config, video)──► API FastAPI + PostgreSQL ◄──HTTP┘
```

- **Nodo de cámara:** captura fotos por horario o por pedido, las guarda primero en la microSD, las
  sube a la API, analiza el color de la planta y transmite video en vivo.
- **Backend** (en la Mac, con Docker): broker MQTT, API y base de datos.
- **Nodo de pantalla:** panel táctil que muestra el estado de todo y manda comandos.

---

## 0. Preparación (antes de grabar)

1. **Backend encendido**. En una terminal:
   ```sh
   cd backend
   export PATH="$HOME/.orbstack/bin:$PATH"
   docker compose up -d
   curl http://localhost:8000/api/v1/health
   ```
   Se espera `"status": "ok"` con `db` y `mqtt` en `ok`.
2. **IP de la Mac**: `ipconfig getifaddr en0` tiene que dar `192.168.100.189` (la que tienen
   grabada las placas). Si cambió, ver CLAUDE.md antes de seguir.
3. **Placas alimentadas**: la cámara y la pantalla, por USB. La cámara apuntando a la planta.
4. **Módulo de control simulado** (todavía no hay hardware de sensores). En otra terminal:
   ```sh
   cd backend && uv run python scripts/ctrl_sim.py
   ```
   Publica cada 10 s temperatura, humedad, CO₂, luz, las variables del sustrato y el estado de
   los relés, como lo haría el módulo real. Opciones: `--mode dwc` (hidroponía), `--hot` (más
   calor: se ve entrar el ventilador), `--period 5`. Ctrl+C lo apaga avisando `offline`.
   Conviene arrancarlo un rato antes de grabar para que el gráfico de 24 h tenga puntos (uno por
   minuto).
5. **Ventanas preparadas** (conviene dejarlas abiertas antes de empezar a grabar):
   - Terminal A — tráfico MQTT en vivo: `cd backend && ./scripts/mqtt_watch.sh`
   - Navegador — galería: `./scripts/open_web.sh gallery`
   - Navegador — documentación de la API: `./scripts/open_web.sh docs`
6. **Cuidado con las claves en el video**: la URL de la galería y de `/live` lleva el token de
   usuario. Abrir las pestañas antes de grabar y no mostrar la barra de direcciones (o achicar la
   ventana / usar pantalla completa). No mostrar `backend/.env` ni `sdkconfig.defaults.local`.
7. **Hora**: la ventana de luz es 06:00–00:00 (hora local). El paso 8 (luz apagada) solo funciona
   dentro de ese horario.

---

## 1. Presentación del sistema (1 min)

- **Mostrar** las dos placas y la pantalla encendida en **Inicio**.
- **Contar**: barra superior con WiFi, nodos en línea, alarmas y hora (la hora la toma del reloj
  con batería apenas arranca, antes de tener red); KPI ambientales (temperatura, humedad, CO₂,
  luz y ciclo) y la tendencia de 24 h, que hoy vienen del **módulo de control simulado**; a la
  derecha, la última captura de la cámara.

## 1b. Módulo de control (simulado) (2 min)

- **Ambiente**: gráfico de temperatura (azul) y humedad (verde) de las últimas 24 h y valores
  actuales (CO₂, lux, ciclo de luz).
- **Cultivo**: las variables de la zona de raíces según el modo de cultivo (sustrato: humedad y
  EC del sustrato; DWC: pH, EC, oxígeno disuelto, temperatura del agua) y el nivel del reservorio.
- **Actuadores**: luces, ventilación y bomba, con su estado y si están en automático. Con el
  simulador se ve la bomba de riego encenderse sola cuando el sustrato baja de 35 %.
- **Contar**: es un simulador que publica exactamente el mensaje que usará el módulo real
  (`contract/mqtt/schemas/ctrl_state.schema.json`); cuando exista el hardware, la pantalla no
  cambia.
- **Probar**: cortar el simulador (Ctrl+C) → los valores pasan a "--" y aparece la alarma de nodo
  sin conexión; volver a arrancarlo con `--mode dwc` → Cultivo muestra las variables de hidroponía.

## 2. Estado en tiempo real por MQTT (2 min)

1. **Mostrar** la Terminal A: cada nodo publica su `status` (estado, firmware, RSSI, SD libre,
   versión de configuración).
2. **Hacer**: desenchufar la cámara.
3. **Esperar** ~45 s: el broker publica el mensaje de "última voluntad" (`node_state: offline`).
4. **Mostrar** en la pantalla: la tarjeta de `cam-01` pasa a **Sin conexión** y sube el contador de
   alarmas (la API abrió un evento `node_disconnected`).
5. **Hacer**: volver a enchufar la cámara. En unos segundos vuelve a **En línea** y el evento se
   resuelve solo.

## 3. Captura programada y galería (2 min)

- La cámara de pruebas captura **cada 1 minuto** (configuración remota, ver paso 7).
- **Mostrar** en la galería del navegador cómo aparecen las capturas nuevas (se actualiza cada
  30 s; también se puede recargar).
- **Mostrar** en la pantalla, **Cámaras**: la miniatura de la tarjeta se actualiza sola con la
  última foto y un chip con el resultado del análisis ("Sin anomalías", "Revisar planta",
  "A oscuras"…).

## 4. Capturar ahora desde la pantalla (1 min)

1. **Hacer**: en **Cámaras**, tocar **Capturar ahora**.
2. **Ver** el botón: *Capturando…* → *✓ Captura lista* → vuelve a *Capturar ahora*.
3. **Mostrar** en la Terminal A el ida y vuelta: `cmd` (`capture_now`) → `cmd/ack` `accepted` →
   `cmd/ack` `done`.
4. Unos segundos después la miniatura cambia por la nueva foto (en resolución completa, UXGA).

## 5. Detalle de cámara (2 min)

1. **Hacer**: tocar la tarjeta de `cam-01` (fuera de los botones).
2. **Resumen**: foto, datos de la captura (hora, disparo, resolución) y el análisis local (verde %,
   amarillo/marrón %, brillo, puntaje vs. umbral), y el estado del nodo.
3. **Historial**: las últimas 12 capturas; tocar una para verla en Resumen.
4. **Configuración**: los parámetros de esa cámara y de dónde sale cada valor (propio, de la zona,
   global o por defecto). Se usa en el paso 7.

## 6. Vista en vivo (3 min)

1. **Hacer**: en **Cámaras**, tocar **▶ En vivo** sobre la miniatura (o desde el detalle).
2. **Ver**: la imagen aparece en menos de un segundo; arriba a la derecha los cuadros por segundo.
3. **Probar**:
   - Guías de encuadre: **Tercios**, **Cruz**, **Grilla** (sirven para acomodar la cámara).
   - Resolución **VGA** / **QVGA** y fps máximos **5 / 10 / 15**.
   - **Captura completa**: la cámara pausa el video, saca la foto en UXGA y sigue transmitiendo.
4. **Opcional**: abrir la misma vista en el navegador (`./scripts/open_web.sh live`): los dos
   espectadores comparten la misma sesión.
5. **Hacer**: **Volver**. La sesión se cierra y la cámara retoma sus capturas programadas.

## 7. Configuración remota (2 min)

1. **Hacer**: en el detalle de `cam-01` → **Configuración**, cambiar **Intervalo de captura** a
   *5 min* y tocar **Guardar**.
2. **Mostrar** en la Terminal A: la API publica `refresh_config`, la cámara responde `done` y su
   `status` trae la nueva `config_version`.
3. **Mostrar** la pantalla **Config**: los parámetros globales (para todas las cámaras: intervalo,
   ventana de luz, umbral del análisis) y los datos de este equipo.
4. **Volver a dejar** el intervalo de `cam-01` en *1 min* (Guardar) para que sigan las pruebas.
   No usar "Usar la global": borra esa configuración de pruebas y la cámara pasa a 2 horas.

## 8. Alarma: luz apagada antes de tiempo (2 min)

> Solo dentro de la ventana de luz (06:00–00:00).

1. **Hacer**: tapar la lente de la cámara (o apagar la luz del ambiente) y esperar la próxima
   captura (≤ 1 min).
2. **Ver**: el análisis detecta escena oscura dentro del horario de luz y la cámara publica el
   evento `early_lights_off` (severidad alta). En la pantalla sube el contador de alarmas.
3. **Hacer**: ir a **Eventos** (filtro *Abiertos*) y tocar **Reconocer** en ese evento. El contador
   baja.
4. Destapar la lente: la cámara vuelve a la normalidad (el evento no se repite mientras siga tapada).

## 9. Sin red no se pierden fotos (2 min)

1. **Hacer**, en una terminal: `cd backend && docker compose stop api`
2. Esperar 2–3 capturas. La cámara las guarda en la microSD y reintenta la subida con espera
   creciente. (La pantalla muestra "Sin conexión con la API" en Eventos/Detalle.)
3. **Hacer**: `docker compose start api`
4. **Mostrar** en la galería: aparecen todas las capturas que quedaron pendientes, en orden.

## 10. Actualización de firmware por WiFi (OTA) (3 min)

1. **Preparar** una versión nueva de la cámara: subir el número en `camera_node/version.txt`
   (por ejemplo `0.2.5`) y compilar:
   ```sh
   cd camera_node && source ~/.espressif/tools/activate_idf_v6.1.sh && idf.py build
   ```
2. **Subirla a la API** (lee versión y tipo de nodo del propio archivo):
   ```sh
   cd ../backend && ./scripts/fw_upload.sh ../camera_node/build/camera_node.bin
   ```
3. **Hacer**: en la pantalla, **Config** → *Firmware (OTA)*: aparece `cam-01 0.2.4` con el botón
   **A 0.2.5**. Tocarlo.
4. **Ver**: *actualizando…*; la cámara baja el firmware (~10–20 s), lo verifica (SHA-256), reinicia
   y, cuando vuelve a conectar, confirma la versión. La fila queda `cam-01 0.2.5 al día`.
5. **Contar**: si el firmware nuevo no lograra conectarse, a los 3 minutos la placa vuelve sola a
   la versión anterior (probado).

## 10b. Cambiar de red o de servidor (aprovisionamiento) (3 min)

- **Pantalla**: **Config** → **Red y servidor…**. Elegir la red de la lista (**Buscar redes**),
  escribir la clave, el servidor (IP o `nombre.local`) y tocar **Guardar y reiniciar**.
  **Valores de fábrica** vuelve a los datos con los que se compiló.
- **Cámara** (no tiene pantalla: arma su propia red WiFi con una página de configuración):
  1. Con la cámara encendida, mantener **BOOT 5 s** (no al enchufarla: así entra en modo de
     grabación por USB). También entra sola si no tiene red guardada o si pasa 5 min sin WiFi.
  2. Desde el celular, conectarse a `MasterGrow-cam-01-XXXX` (la clave sale en el log de la
     placa); la página se abre sola o en `http://192.168.4.1`.
  3. Elegir la red, la clave y el servidor → **Probar y guardar**. La cámara prueba el WiFi y que
     el broker y la API respondan; si todo está bien, guarda y reinicia.

## 11. Cierre (1 min)

- **Mostrar** `http://localhost:8000/docs`: toda la API documentada (OpenAPI), generada desde el
  código y guardada en `contract/openapi.json`.
- **Contar** qué sigue: módulo de control real (sensores y actuadores, hoy simulado), variante de
  la interfaz con LVGL Pro, prueba continua de 24 h.

---

## Si algo no sale

| Síntoma | Qué revisar |
|---|---|
| Nodos en *Sin conexión* desde el arranque | ¿Cambió la IP de la Mac? `ipconfig getifaddr en0` (debe ser `.189`) |
| La pantalla dice "Sin conexión con la API" | `docker compose ps` y `curl localhost:8000/api/v1/health` |
| No llegan fotos a la galería | Terminal A: ¿la cámara publica `status`? ¿La API está arriba? |
| La vista en vivo no arranca | La cámara tiene que estar *En línea*; probar de nuevo con **Reintentar** |
| El botón de OTA no aparece | La versión subida tiene que ser distinta de la que tiene la placa |
| La cámara dice "Poca planta" con la planta a la vista | Evitar contraluz (ventana detrás) y revisar que la lente no tenga la película protectora |
| Quiero ver el log de una placa | Cerrar cualquier monitor abierto y `idf.py -p <puerto> monitor` (salir con Ctrl+]) |
