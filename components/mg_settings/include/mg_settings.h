/**
 * @file mg_settings.h
 * @brief Credenciales de red y servidor de un nodo, guardadas en NVS (Contrato §8.2: "token
 *        almacenado en NVS"). Lo que no está guardado toma el valor de Kconfig, así un nodo recién
 *        grabado funciona igual que antes.
 *
 * El servidor es un solo nombre o IP ("192.168.100.189" o "mastergrow.local"): de ahí salen
 * mqtt://servidor:1883 y http://servidor:8000/api/v1. Con mDNS (lwIP resuelve los .local) el
 * nodo no depende de la IP del servidor.
 *
 * Guardar escribe la flash: llamar desde una tarea con la pila en RAM interna.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];
    char server[64];      /**< nombre o IP del servidor (broker y API) */
    char mqtt_pass[65];   /**< usuario MQTT = node_id */
    char api_token[96];
} mg_settings_t;

/** @brief Lee NVS (con Kconfig para lo que falte). Requiere nvs_flash_init(). */
esp_err_t mg_settings_load(mg_settings_t *out);

/** @brief true si hay algo guardado en NVS (si no, todo sale de Kconfig). */
bool mg_settings_stored(void);

/** @brief Guarda todos los campos. */
esp_err_t mg_settings_save(const mg_settings_t *s);

/** @brief Borra lo guardado: el nodo vuelve a los valores de Kconfig. */
esp_err_t mg_settings_erase(void);

/** @brief Marca/consulta "arrancar en modo configuración" (p. ej. tras no poder conectarse).
 *         take() la lee y la borra. */
esp_err_t mg_settings_request_setup(void);
bool mg_settings_take_setup_request(void);

/** @brief "mqtt://servidor:1883". */
void mg_settings_mqtt_uri(const mg_settings_t *s, char *out, size_t len);

/** @brief "http://servidor:8000/api/v1". */
void mg_settings_api_url(const mg_settings_t *s, char *out, size_t len);

#ifdef __cplusplus
}
#endif
