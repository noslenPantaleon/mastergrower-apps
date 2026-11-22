/**
 * @file jpeg_util.c
 * @brief Ver jpeg_util.h.
 */
#include "jpeg_util.h"

#include "esp_heap_caps.h"
#include "jpeg_decoder.h"

esp_err_t jpeg_to_rgb565(const uint8_t *jpg, size_t len, uint16_t **pixels, uint16_t *w, uint16_t *h)
{
    esp_jpeg_image_cfg_t cfg = {
        .indata = (uint8_t *)jpg,
        .indata_size = len,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,  // sin swap: orden nativo de LVGL
        .out_scale = JPEG_IMAGE_SCALE_0,
    };
    esp_jpeg_image_output_t info;
    esp_err_t err = esp_jpeg_get_image_info(&cfg, &info);
    if (err != ESP_OK) {
        return err;
    }
    size_t bytes = (size_t)info.width * info.height * 2;
    uint16_t *out = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM);
    if (!out) {
        return ESP_ERR_NO_MEM;
    }
    cfg.outbuf = (uint8_t *)out;
    cfg.outbuf_size = bytes;
    esp_jpeg_image_output_t img;
    err = esp_jpeg_decode(&cfg, &img);
    if (err != ESP_OK) {
        heap_caps_free(out);
        return err;
    }
    *pixels = out;
    *w = img.width;
    *h = img.height;
    return ESP_OK;
}
