/**
 * @file config_store.h
 * @brief Configuración efectiva del nodo (Contrato §7, contract/http/schemas/cam_config.schema.json).
 *
 * La API resuelve override → global → default y entrega la config completa con su versión. Este
 * servicio la valida campo por campo (un valor inválido o ausente toma el valor por defecto del
 * firmware), la guarda en NVS y la mantiene en RAM. Al arrancar sin red se usa la última guardada
 * (modo autónomo). Seguro para usar desde cualquier tarea.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t version;                 /**< 0 = valores por defecto del firmware */
    uint16_t capture_interval_min;    /**< 1–1440 */
    char light_start[6];              /**< "HH:MM" hora local */
    char light_end[6];                /**< "HH:MM"; end <= start cruza la medianoche */
    int16_t utc_offset_min;           /**< -720–840 */
    char analysis_mode[8];            /**< hsv | hybrid | model */
    char resolution[8];               /**< rutina: QVGA … QXGA */
    char resolution_full[8];          /**< prioridad alta */
    uint8_t jpeg_quality;             /**< 4–63 */
    float hsv_suspicion_threshold;    /**< 0–1 */
    uint16_t min_trigger_interval_s;  /**< 0–3600 */
    char stream_resolution_max[8];    /**< qvga | vga */
    uint8_t stream_fps_max;           /**< 1–30 */
    uint16_t stream_ttl_max_s;        /**< 10–3600 */
} node_config_t;

/**
 * @brief Carga la config guardada en NVS (si hay) sobre los valores por defecto.
 *        Requiere nvs_flash_init() previo.
 */
esp_err_t config_store_init(const node_config_t *defaults);

/** @brief Copia de la config vigente. */
void config_store_get(node_config_t *out);

uint32_t config_store_version(void);

/**
 * @brief Aplica la respuesta de GET /config/nodes/{id} ({"version", "config", ...}).
 * @param[out] changed  true si cambió algo (versión o valores); en ese caso ya quedó en NVS
 * @return ESP_ERR_INVALID_ARG si el JSON no tiene version/config.
 */
esp_err_t config_store_apply_json(const char *body, size_t len, bool *changed);

/** @brief Config vigente como JSON (para depuración). Liberar con free(). */
char *config_store_to_json(void);

#ifdef __cplusplus
}
#endif
