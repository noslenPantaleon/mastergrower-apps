/**
 * @file hal_lcd.c
 * @brief Ver hal_lcd.h. Tiempos y pines del ejemplo oficial de Waveshare (09_lvgl_v9_demo).
 */
#include "hal_lcd.h"

#include "esp_check.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "hal_board.h"

static const char *TAG = "hal_lcd";

#define PCLK_HZ        (16 * 1000 * 1000)
#define BOUNCE_LINES   10   /**< búfer intermedio en SRAM: el DMA no lee la PSRAM directamente */

esp_err_t hal_lcd_init(int num_fbs, esp_lcd_panel_handle_t *out)
{
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = PCLK_HZ,
            .h_res = HAL_LCD_H_RES,
            .v_res = HAL_LCD_V_RES,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags.pclk_active_neg = 1,
        },
        .data_width = 16,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = num_fbs,
        .bounce_buffer_size_px = HAL_LCD_H_RES * BOUNCE_LINES,
        .dma_burst_size = 64,
        .hsync_gpio_num = 46,
        .vsync_gpio_num = 3,
        .de_gpio_num = 5,
        .pclk_gpio_num = 7,
        .disp_gpio_num = -1,  // DISP va por el CH422G (EXIO2)
        /* RGB565: B3–B7, G2–G7, R3–R7 */
        .data_gpio_nums = { 14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40 },
        .flags.fb_in_psram = 1,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, out), TAG, "panel RGB");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*out), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*out), TAG, "init");
    ESP_LOGI(TAG, "Panel %dx%d RGB565, %d framebuffers en PSRAM, pclk %d MHz", HAL_LCD_H_RES, HAL_LCD_V_RES,
             num_fbs, PCLK_HZ / 1000000);
    return ESP_OK;
}

esp_err_t hal_lcd_backlight(bool on)
{
    return hal_board_exio_set(HAL_EXIO_BACKLIGHT, on);
}
