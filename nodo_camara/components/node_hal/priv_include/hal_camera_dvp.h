/**
 * @file hal_camera_dvp.h
 * @brief Base común para sensores DVP manejados con esp32-camera (OV3660, OV5640, ...).
 *        Cada driver concreto aporta su PID esperado y su ajuste fino del sensor.
 */
#pragma once

#include "hal_camera_driver.h"
#include "esp_camera.h"

typedef struct {
    uint16_t expected_pid;          /**< OV3660_PID, OV5640_PID, ... */
    framesize_t max_framesize;      /**< reserva de framebuffer: la mayor resolución a usar */
    void (*tune)(sensor_t *s);      /**< ajustes propios del sensor; puede ser NULL */
} hal_dvp_sensor_desc_t;

esp_err_t hal_dvp_init(const hal_dvp_sensor_desc_t *desc, const hal_camera_config_t *cfg);
esp_err_t hal_dvp_deinit(void);
esp_err_t hal_dvp_capture(hal_frame_t *out);
void hal_dvp_release(hal_frame_t *frame);
esp_err_t hal_dvp_set_resolution(hal_camera_res_t res);
esp_err_t hal_dvp_set_quality(uint8_t q);
esp_err_t hal_dvp_set_orientation(bool vflip, bool hmirror);
const char *hal_dvp_sensor_name(void);
