/**
 * @file hal_touch.c
 * @brief Ver hal_touch.h.
 *
 * El GT911 elige su dirección según el nivel de INT al salir de reset: INT en bajo → 0x5D.
 * Después de 50 ms INT se suelta (entrada) para que lo maneje el GT911. Se lee por sondeo.
 */
#include "hal_touch.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal_board.h"
#include "hal_lcd.h"

static const char *TAG = "hal_touch";

#define TOUCH_INT_GPIO  4

static esp_err_t gt911_reset(void)
{
    gpio_config_t io = { .pin_bit_mask = 1ULL << TOUCH_INT_GPIO, .mode = GPIO_MODE_OUTPUT };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "INT");
    gpio_set_level(TOUCH_INT_GPIO, 0);
    ESP_RETURN_ON_ERROR(hal_board_exio_set(HAL_EXIO_TOUCH_RST, false), TAG, "RST bajo");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(hal_board_exio_set(HAL_EXIO_TOUCH_RST, true), TAG, "RST alto");
    vTaskDelay(pdMS_TO_TICKS(60));
    ESP_RETURN_ON_ERROR(gpio_set_direction(TOUCH_INT_GPIO, GPIO_MODE_INPUT), TAG, "INT entrada");
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

esp_err_t hal_touch_init(esp_lcd_touch_handle_t *out)
{
    ESP_RETURN_ON_ERROR(gt911_reset(), TAG, "reset GT911");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(hal_board_i2c(), &io_cfg, &io), TAG, "IO I2C");

    const esp_lcd_touch_config_t cfg = {
        .x_max = HAL_LCD_H_RES,
        .y_max = HAL_LCD_V_RES,
        .rst_gpio_num = -1,  // reset por el CH422G (hecho arriba)
        .int_gpio_num = -1,  // sondeo
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(io, &cfg, out), TAG, "GT911 no responde");
    ESP_LOGI(TAG, "GT911 listo (0x%02x)", ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS);
    return ESP_OK;
}
