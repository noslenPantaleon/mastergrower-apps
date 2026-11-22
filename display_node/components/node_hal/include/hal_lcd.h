/**
 * @file hal_lcd.h
 * @brief Panel RGB 800×480 (RGB565, 16 líneas de datos) con framebuffers en PSRAM.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_LCD_H_RES  800
#define HAL_LCD_V_RES  480

/**
 * @brief Crea e inicia el panel. Requiere hal_board_init().
 * @param num_fbs  framebuffers en PSRAM (2 para el modo sin tearing de LVGL)
 */
esp_err_t hal_lcd_init(int num_fbs, esp_lcd_panel_handle_t *out);

/** @brief Retroiluminación (solo encendido/apagado: la placa no tiene PWM de brillo). */
esp_err_t hal_lcd_backlight(bool on);

#ifdef __cplusplus
}
#endif
