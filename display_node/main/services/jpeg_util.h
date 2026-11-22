/**
 * @file jpeg_util.h
 * @brief JPEG → RGB565 (orden nativo de LVGL) en PSRAM con esp_jpeg (tjpgd de la ROM).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/** @brief Decodifica; *pixels queda en PSRAM (liberar con heap_caps_free). */
esp_err_t jpeg_to_rgb565(const uint8_t *jpg, size_t len, uint16_t **pixels, uint16_t *w, uint16_t *h);
