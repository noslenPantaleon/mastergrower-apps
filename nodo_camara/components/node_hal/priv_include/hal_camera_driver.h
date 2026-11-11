/**
 * @file hal_camera_driver.h
 * @brief Contrato interno entre hal_camera.c (despachador) y cada driver de cámara.
 *
 * Para agregar un tipo de cámara nuevo (p. ej. térmica):
 *   1. crear src/hal_camera_<tipo>.c que defina `const hal_camera_driver_t hal_camera_driver`
 *   2. agregar la opción en el Kconfig (choice MG_CAMERA_MODEL) y en CMakeLists.txt
 * El resto del firmware no cambia.
 */
#pragma once

#include "hal_camera.h"

typedef struct {
    const char *camera_type;   /**< valor del enum camera_type del contrato */
    hal_camera_res_t max_res;  /**< resolución máxima soportada */

    esp_err_t (*init)(const hal_camera_config_t *cfg);
    esp_err_t (*deinit)(void);
    esp_err_t (*capture)(hal_frame_t *out);
    void (*release)(hal_frame_t *frame);
    esp_err_t (*set_resolution)(hal_camera_res_t res);
    esp_err_t (*set_quality)(uint8_t q);
    esp_err_t (*set_orientation)(bool vflip, bool hmirror);
    const char *(*sensor_name)(void);
} hal_camera_driver_t;

/** Definido por el único driver compilado (elegido por Kconfig). */
extern const hal_camera_driver_t hal_camera_driver;
