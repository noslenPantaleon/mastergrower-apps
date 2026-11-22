/**
 * @file hal_touch.h
 * @brief Táctil capacitivo GT911 por I2C (reset por el CH422G, INT en GPIO4).
 */
#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Resetea el GT911 (fija su dirección en 0x5D) y crea el manejador. Requiere hal_board_init(). */
esp_err_t hal_touch_init(esp_lcd_touch_handle_t *out);

#ifdef __cplusplus
}
#endif
