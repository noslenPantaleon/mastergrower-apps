/**
 * @file app_main.c
 * @brief Nodo de pantalla MasterGrow (D0): placa, panel RGB, táctil y LVGL 9.
 *
 * LVGL corre en su propia tarea (esp_lvgl_port) en el núcleo 1; la red irá en el núcleo 0 (D1).
 * Sin tearing: 2 framebuffers en PSRAM, LVGL dibuja directo en el que no se muestra y el cambio
 * se hace en el vsync (avoid_tearing + direct_mode).
 */
#include <inttypes.h>
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal_board.h"
#include "hal_lcd.h"
#include "hal_touch.h"
#include "sdkconfig.h"
#include "ui_hello.h"

static const char *TAG = "app";

#define LVGL_TASK_CORE   1
#define LVGL_TASK_PRIO   4
#define LVGL_TASK_STACK  (8 * 1024)

static void log_memory(const char *when)
{
    ESP_LOGI(TAG, "[%s] RAM interna libre %u KB · PSRAM libre %u KB", when,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

static lv_display_t *start_display(esp_lcd_panel_handle_t panel, esp_lcd_touch_handle_t touch)
{
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = LVGL_TASK_PRIO;
    port_cfg.task_stack = LVGL_TASK_STACK;
    port_cfg.task_affinity = LVGL_TASK_CORE;
    if (lvgl_port_init(&port_cfg) != ESP_OK) {
        return NULL;
    }

    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel,
        .buffer_size = HAL_LCD_H_RES * HAL_LCD_V_RES,  // con avoid_tearing usa los fb del panel
        .double_buffer = true,
        .hres = HAL_LCD_H_RES,
        .vres = HAL_LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_spiram = true,
            .direct_mode = true,
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (disp && touch) {
        const lvgl_port_touch_cfg_t touch_cfg = { .disp = disp, .handle = touch };
        if (!lvgl_port_add_touch(&touch_cfg)) {
            ESP_LOGW(TAG, "No se pudo registrar el táctil en LVGL");
        }
    }
    return disp;
}

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "MasterGrow nodo de pantalla · node_id=%s · fw %s · IDF %s", CONFIG_MG_NODE_ID,
             CONFIG_MG_FIRMWARE_VERSION, app->idf_ver);
    if (!esp_psram_is_initialized()) {
        ESP_LOGE(TAG, "PSRAM no disponible: los framebuffers necesitan PSRAM");
        return;
    }
    log_memory("arranque");

    ESP_ERROR_CHECK(hal_board_init());
    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(hal_lcd_init(2, &panel));
    esp_lcd_touch_handle_t touch = NULL;
    esp_err_t err = hal_touch_init(&touch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Táctil no disponible (%s): la pantalla sigue sin él", esp_err_to_name(err));
        touch = NULL;
    }

    lv_display_t *disp = start_display(panel, touch);
    if (!disp) {
        ESP_LOGE(TAG, "No se pudo iniciar LVGL");
        return;
    }
    if (lvgl_port_lock(0)) {
        ui_hello_create();
        lvgl_port_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(100));  // primera imagen ya dibujada: recién ahí la luz
    ESP_ERROR_CHECK(hal_lcd_backlight(true));
    log_memory("interfaz lista");
}
