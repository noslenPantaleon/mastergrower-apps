/**
 * @file mg_wifi.h
 * @brief Conexión WiFi en modo estación con reconexión automática (backoff exponencial).
 *
 * Requisitos previos del llamador (una sola vez en app_main):
 *   nvs_flash_init(), esp_netif_init(), esp_event_loop_create_default().
 *
 * Eventos publicados en el event loop por defecto (base MG_WIFI_EVENT):
 *   MG_WIFI_EVENT_CONNECTED     — IP obtenida (data: esp_netif_ip_info_t)
 *   MG_WIFI_EVENT_DISCONNECTED  — se perdió la conexión (data: uint8_t reason)
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(MG_WIFI_EVENT);

typedef enum {
    MG_WIFI_EVENT_CONNECTED,
    MG_WIFI_EVENT_DISCONNECTED,
} mg_wifi_event_id_t;

typedef struct {
    const char *ssid;      /**< NULL → CONFIG_MG_WIFI_SSID */
    const char *password;  /**< NULL → CONFIG_MG_WIFI_PASSWORD */
    const char *hostname;  /**< NULL → hostname por defecto de esp_netif (recomendado: node_id) */
} mg_wifi_config_t;

/**
 * @brief Inicia WiFi STA y el primer intento de conexión. No bloquea.
 * @param cfg  Configuración; NULL usa todos los valores de Kconfig.
 */
esp_err_t mg_wifi_start(const mg_wifi_config_t *cfg);

/** @brief Detiene WiFi y libera recursos. */
esp_err_t mg_wifi_stop(void);

/** @brief Bloquea hasta tener IP o hasta vencer el timeout. ESP_ERR_TIMEOUT si vence. */
esp_err_t mg_wifi_wait_connected(TickType_t timeout);

/** @brief true si hay IP asignada. */
bool mg_wifi_is_connected(void);

/** @brief RSSI del AP actual en dBm; 0 si no hay conexión. */
int8_t mg_wifi_get_rssi(void);

/** Red encontrada por mg_wifi_scan(). */
typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secured;   /**< false = red abierta */
} mg_wifi_ap_t;

/**
 * @brief Busca redes (bloquea ~2–3 s; no llamar desde la tarea de LVGL). Sin repetidos, de la más
 *        fuerte a la más débil. Pausa los reintentos de conexión mientras busca.
 * @return cantidad de redes (≤ max) o -1 si falló.
 */
int mg_wifi_scan(mg_wifi_ap_t *out, int max);

/** @brief IP actual como texto ("0.0.0.0" si no hay conexión). */
esp_err_t mg_wifi_get_ip_str(char *buf, size_t len);

#ifdef __cplusplus
}
#endif
