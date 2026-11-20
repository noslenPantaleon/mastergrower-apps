/**
 * @file mg_api.h
 * @brief Cliente HTTP de la API MasterGrow (Contrato §5): URL base y token Bearer por Kconfig,
 *        POST multipart con escritura en streaming (no arma el cuerpo completo en memoria) y GET.
 *
 * Cada llamada abre y cierra su conexión: simple y robusto ante cortes de WiFi. Seguro desde
 * varias tareas (no comparte estado).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Una parte de un cuerpo multipart/form-data. */
typedef struct {
    const char *name;          /**< nombre del campo */
    const char *filename;      /**< NULL = campo de formulario sin archivo */
    const char *content_type;  /**< NULL = sin Content-Type */
    const void *data;
    size_t len;
} mg_api_part_t;

/** Resultado de una solicitud que llegó a recibir respuesta. */
typedef struct {
    int status;        /**< código HTTP */
    char *body;        /**< respuesta terminada en '\0' (malloc; liberar con free) o NULL */
    size_t body_len;
} mg_api_resp_t;

/**
 * @brief POST multipart a <base><path>.
 * @param idempotency_key  NULL o valor del encabezado Idempotency-Key
 * @param max_body         bytes máximos a guardar de la respuesta (el resto se descarta)
 * @return ESP_OK si hubo respuesta HTTP (mirar resp->status, aunque sea 4xx/5xx);
 *         otro código si falló la red (resp->status = 0).
 */
esp_err_t mg_api_post_multipart(const char *path, const mg_api_part_t *parts, size_t n_parts,
                                const char *idempotency_key, size_t max_body, mg_api_resp_t *resp);

/** @brief GET a <base><path>. Mismas reglas de retorno que mg_api_post_multipart. */
esp_err_t mg_api_get(const char *path, size_t max_body, mg_api_resp_t *resp);

void mg_api_resp_free(mg_api_resp_t *resp);

/** @brief URL base configurada (para logs). */
const char *mg_api_base_url(void);

#ifdef __cplusplus
}
#endif
