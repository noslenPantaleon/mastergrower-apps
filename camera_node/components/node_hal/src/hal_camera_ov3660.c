/**
 * @file hal_camera_ov3660.c
 * @brief Driver xiao-ov3660: OV3660 integrada en el XIAO ESP32-S3 Sense (3 MP, QXGA máx.).
 */
#include "hal_camera_dvp.h"

/* Brillo/contraste/saturación: los aplica hal_camera_init con HAL_CAMERA_TUNING_DEFAULT()
 * (Kconfig). El ejemplo de Espressif usaba saturación -2: las hojas salían casi grises. */
static const hal_dvp_sensor_desc_t k_ov3660 = {
    .expected_pid = OV3660_PID,
    .max_framesize = FRAMESIZE_QXGA,
    .tune = NULL,
};

static esp_err_t ov3660_init(const hal_camera_config_t *cfg)
{
    return hal_dvp_init(&k_ov3660, cfg);
}

const hal_camera_driver_t hal_camera_driver = {
    .camera_type = "xiao-ov3660",
    .max_res = HAL_CAM_RES_QXGA,
    .init = ov3660_init,
    .deinit = hal_dvp_deinit,
    .capture = hal_dvp_capture,
    .release = hal_dvp_release,
    .set_resolution = hal_dvp_set_resolution,
    .set_quality = hal_dvp_set_quality,
    .set_orientation = hal_dvp_set_orientation,
    .set_tuning = hal_dvp_set_tuning,
    .sensor_name = hal_dvp_sensor_name,
};
