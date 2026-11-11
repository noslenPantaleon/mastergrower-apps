/**
 * @file hal_camera_dvp_common.c
 * @brief Implementación común sobre esp32-camera para sensores DVP del XIAO ESP32-S3 Sense.
 */
#include "hal_camera_dvp.h"

#include "board_xiao_esp32s3_sense.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

static const char *TAG = "hal_cam_dvp";

#if CONFIG_MG_CAMERA_VFLIP
#define DEFAULT_VFLIP 1
#else
#define DEFAULT_VFLIP 0
#endif
#if CONFIG_MG_CAMERA_HMIRROR
#define DEFAULT_HMIRROR 1
#else
#define DEFAULT_HMIRROR 0
#endif

static const framesize_t k_res_to_fs[HAL_CAM_RES_COUNT] = {
    [HAL_CAM_RES_QVGA] = FRAMESIZE_QVGA,
    [HAL_CAM_RES_VGA]  = FRAMESIZE_VGA,
    [HAL_CAM_RES_SVGA] = FRAMESIZE_SVGA,
    [HAL_CAM_RES_XGA]  = FRAMESIZE_XGA,
    [HAL_CAM_RES_HD]   = FRAMESIZE_HD,
    [HAL_CAM_RES_SXGA] = FRAMESIZE_SXGA,
    [HAL_CAM_RES_UXGA] = FRAMESIZE_UXGA,
    [HAL_CAM_RES_QXGA] = FRAMESIZE_QXGA,
};

static const hal_dvp_sensor_desc_t *s_desc;

esp_err_t hal_dvp_init(const hal_dvp_sensor_desc_t *desc, const hal_camera_config_t *cfg)
{
    const camera_config_t cam = {
        .pin_pwdn = BOARD_CAM_PIN_PWDN,
        .pin_reset = BOARD_CAM_PIN_RESET,
        .pin_xclk = BOARD_CAM_PIN_XCLK,
        .pin_sccb_sda = BOARD_CAM_PIN_SIOD,
        .pin_sccb_scl = BOARD_CAM_PIN_SIOC,
        .pin_d7 = BOARD_CAM_PIN_D7,
        .pin_d6 = BOARD_CAM_PIN_D6,
        .pin_d5 = BOARD_CAM_PIN_D5,
        .pin_d4 = BOARD_CAM_PIN_D4,
        .pin_d3 = BOARD_CAM_PIN_D3,
        .pin_d2 = BOARD_CAM_PIN_D2,
        .pin_d1 = BOARD_CAM_PIN_D1,
        .pin_d0 = BOARD_CAM_PIN_D0,
        .pin_vsync = BOARD_CAM_PIN_VSYNC,
        .pin_href = BOARD_CAM_PIN_HREF,
        .pin_pclk = BOARD_CAM_PIN_PCLK,

        .xclk_freq_hz = CONFIG_MG_CAMERA_XCLK_HZ,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_JPEG,
        /* Los framebuffers JPEG se dimensionan con frame_size al iniciar: se reserva para la
         * resolución máxima y después se baja a la pedida, así se puede subir sin reiniciar. */
        .frame_size = desc->max_framesize,
        .jpeg_quality = cfg->jpeg_quality,
        .fb_count = CONFIG_MG_CAMERA_FB_COUNT,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CONFIG_MG_CAMERA_FB_COUNT > 1 ? CAMERA_GRAB_LATEST : CAMERA_GRAB_WHEN_EMPTY,
    };

    ESP_RETURN_ON_ERROR(esp_camera_init(&cam), TAG, "esp_camera_init");

    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        esp_camera_deinit();
        return ESP_FAIL;
    }
    if (s->id.PID != desc->expected_pid) {
        ESP_LOGW(TAG, "Sensor detectado PID=0x%04x, se esperaba 0x%04x (¿camera_type mal configurado?)",
                 s->id.PID, desc->expected_pid);
    }
    s_desc = desc;

    if (desc->tune) {
        desc->tune(s);
    }
    s->set_vflip(s, DEFAULT_VFLIP);
    s->set_hmirror(s, DEFAULT_HMIRROR);

    ESP_RETURN_ON_ERROR(hal_dvp_set_resolution(cfg->resolution), TAG, "resolución inicial");
    ESP_LOGI(TAG, "Cámara %s lista (XCLK %d Hz, %d fb en PSRAM)", hal_dvp_sensor_name(),
             CONFIG_MG_CAMERA_XCLK_HZ, CONFIG_MG_CAMERA_FB_COUNT);
    return ESP_OK;
}

esp_err_t hal_dvp_deinit(void)
{
    s_desc = NULL;
    return esp_camera_deinit();
}

esp_err_t hal_dvp_capture(hal_frame_t *out)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        return ESP_FAIL;
    }
    if (fb->format != PIXFORMAT_JPEG || fb->len == 0) {
        esp_camera_fb_return(fb);
        return ESP_ERR_INVALID_RESPONSE;
    }
    *out = (hal_frame_t){
        .data = fb->buf,
        .len = fb->len,
        .width = fb->width,
        .height = fb->height,
        .timestamp_us = esp_timer_get_time(),
        .priv = fb,
    };
    return ESP_OK;
}

void hal_dvp_release(hal_frame_t *frame)
{
    if (frame && frame->priv) {
        esp_camera_fb_return((camera_fb_t *)frame->priv);
        frame->priv = NULL;
        frame->data = NULL;
        frame->len = 0;
    }
}

esp_err_t hal_dvp_set_resolution(hal_camera_res_t res)
{
    if (res >= HAL_CAM_RES_COUNT || !s_desc || k_res_to_fs[res] > s_desc->max_framesize) {
        return ESP_ERR_INVALID_ARG;
    }
    sensor_t *s = esp_camera_sensor_get();
    return (s && s->set_framesize(s, k_res_to_fs[res]) == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t hal_dvp_set_quality(uint8_t q)
{
    if (q > 63) {
        return ESP_ERR_INVALID_ARG;
    }
    sensor_t *s = esp_camera_sensor_get();
    return (s && s->set_quality(s, q) == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t hal_dvp_set_orientation(bool vflip, bool hmirror)
{
    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        return ESP_ERR_INVALID_STATE;
    }
    s->set_vflip(s, vflip ? 1 : 0);
    s->set_hmirror(s, hmirror ? 1 : 0);
    return ESP_OK;
}

const char *hal_dvp_sensor_name(void)
{
    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        return "none";
    }
    camera_sensor_info_t *info = esp_camera_sensor_get_info(&s->id);
    return info ? info->name : "unknown";
}
