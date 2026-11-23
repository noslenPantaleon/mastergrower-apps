/**
 * @file hal_camera.c
 * @brief Despachador de hal_camera: serializa el acceso al sensor (captura, stream y
 *        cambios de configuración comparten la misma cámara) y delega en el driver compilado.
 */
#include "hal_camera.h"

#include <strings.h>
#include "esp_log.h"
#include "freertos/semphr.h"
#include "hal_camera_driver.h"

static const char *TAG = "hal_camera";

static const struct {
    const char *name;
    uint16_t w, h;
} k_res[HAL_CAM_RES_COUNT] = {
    [HAL_CAM_RES_QVGA] = { "QVGA", 320, 240 },
    [HAL_CAM_RES_VGA]  = { "VGA", 640, 480 },
    [HAL_CAM_RES_SVGA] = { "SVGA", 800, 600 },
    [HAL_CAM_RES_XGA]  = { "XGA", 1024, 768 },
    [HAL_CAM_RES_HD]   = { "HD", 1280, 720 },
    [HAL_CAM_RES_SXGA] = { "SXGA", 1280, 1024 },
    [HAL_CAM_RES_UXGA] = { "UXGA", 1600, 1200 },
    [HAL_CAM_RES_QXGA] = { "QXGA", 2048, 1536 },
};

static struct {
    SemaphoreHandle_t lock;
    bool ready;
    hal_camera_res_t res;
    uint8_t quality;
    uint32_t ok;
    uint32_t failed;
    hal_camera_tuning_t tuning;
} s_cam;

#define LOCK_TIMEOUT pdMS_TO_TICKS(5000)

static bool lock(TickType_t t) { return xSemaphoreTake(s_cam.lock, t) == pdTRUE; }
static void unlock(void) { xSemaphoreGive(s_cam.lock); }

esp_err_t hal_camera_init(const hal_camera_config_t *cfg)
{
    if (s_cam.ready) {
        return ESP_ERR_INVALID_STATE;
    }
    hal_camera_config_t def = HAL_CAMERA_CONFIG_DEFAULT();
    if (!cfg) {
        cfg = &def;
    }
    if (cfg->resolution > hal_camera_driver.max_res || cfg->jpeg_quality > 63) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_cam.lock) {
        s_cam.lock = xSemaphoreCreateMutex();
        if (!s_cam.lock) {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t err = hal_camera_driver.init(cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al iniciar %s: %s", hal_camera_driver.camera_type, esp_err_to_name(err));
        return err;
    }
    s_cam.res = cfg->resolution;
    s_cam.quality = cfg->jpeg_quality;
    s_cam.ok = s_cam.failed = 0;
    s_cam.ready = true;
    hal_camera_tuning_t tun = HAL_CAMERA_TUNING_DEFAULT();
    if (hal_camera_set_tuning(&tun) != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo aplicar el ajuste de imagen por defecto");
    }
    ESP_LOGI(TAG, "camera_type=%s sensor=%s res=%s q=%u", hal_camera_driver.camera_type,
             hal_camera_driver.sensor_name(), k_res[s_cam.res].name, s_cam.quality);
    return ESP_OK;
}

esp_err_t hal_camera_deinit(void)
{
    if (!s_cam.ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!lock(LOCK_TIMEOUT)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = hal_camera_driver.deinit();
    s_cam.ready = false;
    unlock();
    return err;
}

esp_err_t hal_camera_capture(hal_frame_t *out, TickType_t timeout)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_cam.ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!lock(timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = hal_camera_driver.capture(out);
    if (err == ESP_OK) {
        s_cam.ok++;
    } else {
        s_cam.failed++;
        ESP_LOGW(TAG, "Captura fallida: %s", esp_err_to_name(err));
    }
    unlock();
    return err;
}

void hal_camera_release(hal_frame_t *frame)
{
    if (frame && frame->priv) {
        hal_camera_driver.release(frame);
    }
}

esp_err_t hal_camera_set_resolution(hal_camera_res_t res)
{
    if (!s_cam.ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (res > hal_camera_driver.max_res) {
        return ESP_ERR_INVALID_ARG;
    }
    if (res == s_cam.res) {
        return ESP_OK;
    }
    if (!lock(LOCK_TIMEOUT)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = hal_camera_driver.set_resolution(res);
    if (err == ESP_OK) {
        s_cam.res = res;
    }
    unlock();
    return err;
}

esp_err_t hal_camera_set_quality(uint8_t q)
{
    if (!s_cam.ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!lock(LOCK_TIMEOUT)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = hal_camera_driver.set_quality(q);
    if (err == ESP_OK) {
        s_cam.quality = q;
    }
    unlock();
    return err;
}

esp_err_t hal_camera_set_orientation(bool vflip, bool hmirror)
{
    if (!s_cam.ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!lock(LOCK_TIMEOUT)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = hal_camera_driver.set_orientation(vflip, hmirror);
    unlock();
    return err;
}

static bool tuning_ok(int8_t v) { return v >= -2 && v <= 2; }

esp_err_t hal_camera_set_tuning(const hal_camera_tuning_t *t)
{
    if (!t || !tuning_ok(t->brightness) || !tuning_ok(t->contrast) || !tuning_ok(t->saturation)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_cam.ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!hal_camera_driver.set_tuning) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!lock(LOCK_TIMEOUT)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = hal_camera_driver.set_tuning(t);
    if (err == ESP_OK) {
        s_cam.tuning = *t;
        ESP_LOGI(TAG, "Ajuste de imagen: brillo %d, contraste %d, saturación %d",
                 t->brightness, t->contrast, t->saturation);
    }
    unlock();
    return err;
}

esp_err_t hal_camera_get_tuning(hal_camera_tuning_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = s_cam.tuning;
    return ESP_OK;
}

esp_err_t hal_camera_discard_frames(uint8_t n)
{
    for (uint8_t i = 0; i < n; i++) {
        hal_frame_t f;
        esp_err_t err = hal_camera_capture(&f, LOCK_TIMEOUT);
        if (err != ESP_OK) {
            return err;
        }
        hal_camera_release(&f);
    }
    return ESP_OK;
}

esp_err_t hal_camera_get_info(hal_camera_info_t *info)
{
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }
    *info = (hal_camera_info_t){
        .camera_type = hal_camera_driver.camera_type,
        .sensor_name = s_cam.ready ? hal_camera_driver.sensor_name() : "none",
        .resolution = s_cam.res,
        .jpeg_quality = s_cam.quality,
        .captures_ok = s_cam.ok,
        .captures_failed = s_cam.failed,
    };
    return ESP_OK;
}

const char *hal_camera_type(void)
{
    return hal_camera_driver.camera_type;
}

const char *hal_camera_res_name(hal_camera_res_t res)
{
    return res < HAL_CAM_RES_COUNT ? k_res[res].name : NULL;
}

bool hal_camera_res_from_name(const char *name, hal_camera_res_t *out)
{
    if (!name || !out) {
        return false;
    }
    for (int i = 0; i < HAL_CAM_RES_COUNT; i++) {
        if (strcasecmp(name, k_res[i].name) == 0) {
            *out = (hal_camera_res_t)i;
            return true;
        }
    }
    return false;
}

void hal_camera_res_size(hal_camera_res_t res, uint16_t *w, uint16_t *h)
{
    uint16_t ww = 0, hh = 0;
    if (res < HAL_CAM_RES_COUNT) {
        ww = k_res[res].w;
        hh = k_res[res].h;
    }
    if (w) {
        *w = ww;
    }
    if (h) {
        *h = hh;
    }
}
