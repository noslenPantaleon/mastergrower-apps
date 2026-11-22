/**
 * @file hal_board.h
 * @brief Recursos compartidos de la placa Waveshare ESP32-S3-Touch-LCD-4.3B: bus I2C (táctil,
 *        RTC, expansor y conector externo) y expansor de E/S CH422G.
 *
 * Pines (wiki de Waveshare y ejemplo oficial 09_lvgl_v9_demo):
 *   I2C: SDA GPIO8, SCL GPIO9 (400 kHz). Táctil GT911 0x5D, RTC PCF85063 0x51.
 *   CH422G (salidas EXIO0–7, escritas todas juntas):
 *     EXIO1 reset del táctil · EXIO2 retroiluminación (DISP) · EXIO3 reset del LCD
 *     EXIO4 CS de la microSD (activo en bajo) · EXIO0/EXIO5 entradas aisladas DI0/DI1
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_EXIO_TOUCH_RST  (1u << 1)
#define HAL_EXIO_BACKLIGHT  (1u << 2)
#define HAL_EXIO_LCD_RST    (1u << 3)
#define HAL_EXIO_SD_CS      (1u << 4)

/** @brief Inicia el bus I2C y el CH422G (retroiluminación apagada, LCD fuera de reset). */
esp_err_t hal_board_init(void);

/** @brief Bus I2C compartido (NULL antes de hal_board_init). */
i2c_master_bus_handle_t hal_board_i2c(void);

/** @brief Pone en `level` las salidas del CH422G indicadas en `mask` (HAL_EXIO_*). */
esp_err_t hal_board_exio_set(uint8_t mask, bool level);

#ifdef __cplusplus
}
#endif
