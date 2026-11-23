/**
 * @file hal_camera.h
 * @brief Interfaz común de cámara del nodo. La lógica de captura solo usa estas funciones
 *        y no sabe qué sensor hay detrás (Arquitectura Nodo de Cámara §3.1.1).
 *
 * Uso típico:
 *   hal_camera_config_t cfg = HAL_CAMERA_CONFIG_DEFAULT();
 *   hal_camera_init(&cfg);
 *   hal_frame_t f;
 *   if (hal_camera_capture(&f, pdMS_TO_TICKS(2000)) == ESP_OK) {
 *       ... usar f.data / f.len (JPEG) ...
 *       hal_camera_release(&f);   // SIEMPRE devolver el buffer
 *   }
 *
 * Concurrencia: las funciones son seguras para llamarse desde varias tareas (mutex interno),
 * pero solo puede haber un cuadro tomado a la vez por cada framebuffer configurado.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Resoluciones soportadas por el contrato y el análisis. */
typedef enum {
    HAL_CAM_RES_QVGA = 0, /**< 320x240   — stream */
    HAL_CAM_RES_VGA,      /**< 640x480   — stream */
    HAL_CAM_RES_SVGA,     /**< 800x600   — rutina por defecto */
    HAL_CAM_RES_XGA,      /**< 1024x768 */
    HAL_CAM_RES_HD,       /**< 1280x720 */
    HAL_CAM_RES_SXGA,     /**< 1280x1024 */
    HAL_CAM_RES_UXGA,     /**< 1600x1200 */
    HAL_CAM_RES_QXGA,     /**< 2048x1536 — máxima de la OV3660 */
    HAL_CAM_RES_COUNT
} hal_camera_res_t;

typedef struct {
    hal_camera_res_t resolution; /**< resolución inicial */
    uint8_t jpeg_quality;        /**< 0..63, menor = mejor calidad y archivo más grande */
} hal_camera_config_t;

#define HAL_CAMERA_CONFIG_DEFAULT() { .resolution = HAL_CAM_RES_SVGA, .jpeg_quality = 12 }

/** Cuadro capturado (JPEG). Los datos viven en PSRAM y pertenecen al driver. */
typedef struct {
    const uint8_t *data;
    size_t len;
    uint16_t width;
    uint16_t height;
    int64_t timestamp_us; /**< esp_timer_get_time() al momento de la captura */
    void *priv;           /**< uso interno del driver: no tocar */
} hal_frame_t;

/** Estado/diagnóstico de la cámara. */
typedef struct {
    const char *camera_type;     /**< p. ej. "xiao-ov3660" (enum del contrato) */
    const char *sensor_name;     /**< nombre detectado, p. ej. "OV3660" */
    hal_camera_res_t resolution;
    uint8_t jpeg_quality;
    uint32_t captures_ok;
    uint32_t captures_failed;
} hal_camera_info_t;

esp_err_t hal_camera_init(const hal_camera_config_t *cfg);
esp_err_t hal_camera_deinit(void);

/**
 * @brief Captura un cuadro JPEG.
 * @param out      cuadro de salida; liberar con hal_camera_release()
 * @param timeout  espera máxima para tomar la cámara (si otra tarea la está usando)
 * @return ESP_OK, ESP_ERR_TIMEOUT, ESP_ERR_INVALID_STATE (no inicializada) o ESP_FAIL
 */
esp_err_t hal_camera_capture(hal_frame_t *out, TickType_t timeout);
void hal_camera_release(hal_frame_t *frame);

esp_err_t hal_camera_set_resolution(hal_camera_res_t res);
esp_err_t hal_camera_set_quality(uint8_t jpeg_quality);
esp_err_t hal_camera_set_orientation(bool vflip, bool hmirror);

/** Ajuste de imagen del sensor, cada valor en -2..2 (0 = de fábrica). */
typedef struct {
    int8_t brightness;
    int8_t contrast;
    int8_t saturation;
} hal_camera_tuning_t;

#define HAL_CAMERA_TUNING_DEFAULT() { .brightness = CONFIG_MG_CAMERA_BRIGHTNESS, \
    .contrast = CONFIG_MG_CAMERA_CONTRAST, .saturation = CONFIG_MG_CAMERA_SATURATION }

/** @return ESP_ERR_INVALID_ARG fuera de rango, ESP_ERR_NOT_SUPPORTED si el driver no lo tiene. */
esp_err_t hal_camera_set_tuning(const hal_camera_tuning_t *t);
esp_err_t hal_camera_get_tuning(hal_camera_tuning_t *out);

/**
 * @brief Descarta n cuadros. Útil tras el arranque o un cambio de resolución,
 *        mientras se estabilizan la exposición y el balance de blancos.
 */
esp_err_t hal_camera_discard_frames(uint8_t n);

esp_err_t hal_camera_get_info(hal_camera_info_t *info);

/** @brief camera_type del contrato para el driver compilado ("xiao-ov3660"). */
const char *hal_camera_type(void);

/** @brief Nombre corto de una resolución ("SVGA"); NULL si es inválida. */
const char *hal_camera_res_name(hal_camera_res_t res);

/** @brief Inversa de hal_camera_res_name (no distingue mayúsculas); false si no existe. */
bool hal_camera_res_from_name(const char *name, hal_camera_res_t *out);

/** @brief Ancho/alto en píxeles de una resolución. */
void hal_camera_res_size(hal_camera_res_t res, uint16_t *w, uint16_t *h);

#ifdef __cplusplus
}
#endif
