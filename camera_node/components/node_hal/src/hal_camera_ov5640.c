/**
 * @file hal_camera_ov5640.c
 * @brief Driver xiao-ov5640: módulo OV5640 opcional (5 MP). EN VALIDACIÓN: más memoria y
 *        menos fps que la OV3660. El autofoco, si se usa, necesita cargar su firmware aparte.
 */
#include "hal_camera_dvp.h"

static const hal_dvp_sensor_desc_t k_ov5640 = {
    .expected_pid = OV5640_PID,
    /* Se limita a QXGA para no reservar framebuffers de 5 MP hasta validar memoria. */
    .max_framesize = FRAMESIZE_QXGA,
    .tune = NULL,
};

static esp_err_t ov5640_init(const hal_camera_config_t *cfg)
{
    return hal_dvp_init(&k_ov5640, cfg);
}

const hal_camera_driver_t hal_camera_driver = {
    .camera_type = "xiao-ov5640",
    .max_res = HAL_CAM_RES_QXGA,
    .init = ov5640_init,
    .deinit = hal_dvp_deinit,
    .capture = hal_dvp_capture,
    .release = hal_dvp_release,
    .set_resolution = hal_dvp_set_resolution,
    .set_quality = hal_dvp_set_quality,
    .set_orientation = hal_dvp_set_orientation,
    .sensor_name = hal_dvp_sensor_name,
};
