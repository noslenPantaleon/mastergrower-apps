/**
 * @file mg_portal.h
 * @brief Portal de configuración para nodos sin pantalla (la cámara; luego el control).
 *
 * Crea la red WiFi "MasterGrow-<node_id>-XXXX" (WPA2; contraseña "mg" + 6 últimos dígitos hex
 * de la MAC, se imprime en el log) y sirve en http://192.168.4.1 una página para elegir la red,
 * escribir el servidor, la contraseña MQTT y el token. Un DNS que responde todo con 192.168.4.1
 * hace que el celular abra la página solo (portal cautivo).
 *
 * Antes de guardar prueba la red WiFi y que el servidor responda (MQTT y API); si todo da bien
 * guarda en NVS (mg_settings) y reinicia. Sin actividad en `timeout_s`, reinicia en modo normal.
 * Requiere esp_netif_init() y el event loop por defecto; no inicia WiFi antes de llamarlo.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *node_id;
    int timeout_s;        /**< 0 = 900 s */
} mg_portal_config_t;

/** @brief No vuelve: termina reiniciando el nodo. */
void mg_portal_run(const mg_portal_config_t *cfg);

#ifdef __cplusplus
}
#endif
