/**
 * @file mg_ota.h
 * @brief Actualización OTA de los nodos (contract/http/firmware.md, acción ota_update).
 *
 * - mg_ota_start(): verifica versión y proyecto con el comienzo de la imagen, la descarga por
 *   HTTP (token Bearer de mg_api) calculando el SHA-256 del archivo completo, la graba en la
 *   partición OTA libre, guarda el cmd_id en NVS y reinicia.
 * - Al arrancar con una imagen nueva (estado "pendiente de verificación"), mg_ota_init() arma un
 *   temporizador: si en CONFIG_MG_OTA_CONFIRM_TIMEOUT_S no llegan mg_ota_note_mqtt_ok() y
 *   mg_ota_note_api_ok(), vuelve a la versión anterior. Con ambas, confirma la imagen y llama
 *   on_confirmed con el cmd_id guardado (para mandar el ack "done").
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *cmd_id;
    const char *url;      /**< ruta (/api/v1/firmware/{id}/download) o URL absoluta */
    const char *version;  /**< versión esperada (esp_app_desc_t.version) */
    const char *sha256;   /**< 64 dígitos hex del archivo completo */
} mg_ota_req_t;

/** El comando falló después de aceptarlo (reason: download_failed, bad_checksum, ...). */
typedef void (*mg_ota_fail_cb_t)(const char *cmd_id, const char *reason);
/** Se confirmó la imagen nueva; cmd_id es el del ota_update que la instaló ("" si no se sabe). */
typedef void (*mg_ota_confirmed_cb_t)(const char *cmd_id, const char *version);

/** @brief Llamar al arrancar, con NVS ya iniciado. */
esp_err_t mg_ota_init(mg_ota_fail_cb_t on_fail, mg_ota_confirmed_cb_t on_confirmed);

/**
 * @brief Valida y lanza la descarga en una tarea. Si devuelve error, `reason` dice por qué
 *        (same_version, busy, invalid_format) para el ack "rejected".
 */
esp_err_t mg_ota_start(const mg_ota_req_t *req, char *reason, size_t reason_len);

/** @brief true mientras la imagen en uso espera confirmación. */
bool mg_ota_pending(void);

/**
 * @brief Señales para confirmar una imagen nueva (llamarlas siempre; si no hay nada pendiente, no
 *        hacen nada). Se pueden llamar desde cualquier tarea, incluso con la pila en PSRAM: la
 *        confirmación (que escribe la flash) corre en una tarea propia con pila interna.
 */
void mg_ota_note_mqtt_ok(void);
void mg_ota_note_api_ok(void);

#ifdef __cplusplus
}
#endif
