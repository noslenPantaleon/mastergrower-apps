/**
 * @file app_main.c
 * @brief Nodo de pantalla MasterGrow: placa, panel RGB, táctil, LVGL 9 (D0) y datos por MQTT (D1).
 *
 * LVGL corre en su propia tarea (esp_lvgl_port) en el núcleo 1 y es la única que toca la
 * interfaz; la red y el state_store van en el núcleo 0 y avisan los cambios por ui_bridge.
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
#include "net_task.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "state_store.h"
#include "system_config.h"
#include "ui_bridge.h"
#include "api_jobs.h"
#include "cJSON.h"
#include "command_publisher.h"
#include "live_view.h"
#include "thumb_service.h"
#include "ui_api.h"

static const char *TAG = "app";


static void log_memory(const char *when)
{
    ESP_LOGI(TAG, "[%s] RAM interna libre %u KB · PSRAM libre %u KB", when,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

static lv_display_t *start_display(esp_lcd_panel_handle_t panel, esp_lcd_touch_handle_t touch)
{
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = SYS_LVGL_PRIO;
    port_cfg.task_stack = SYS_LVGL_STACK;
    port_cfg.task_stack_caps = MALLOC_CAP_SPIRAM;  // la tarea de LVGL no escribe la flash
    port_cfg.task_affinity = SYS_CORE_UI;
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

static void *json_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

/**
 * cJSON arma cada respuesta de la API con cientos de nodos chicos; con malloc iban a la RAM
 * interna (todo pedido < 16 KB) y una lista de eventos la dejaba en 7 KB libres. En PSRAM.
 */
static void json_to_psram(void)
{
    cJSON_Hooks hooks = { .malloc_fn = json_malloc, .free_fn = heap_caps_free };
    cJSON_InitHooks(&hooks);
}

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "MasterGrow nodo de pantalla · node_id=%s · fw %s · IDF %s", SYS_NODE_ID,
             SYS_FIRMWARE_VERSION, app->idf_ver);
    if (!esp_psram_is_initialized()) {
        ESP_LOGE(TAG, "PSRAM no disponible: los framebuffers necesitan PSRAM");
        return;
    }
    log_memory("arranque");
    json_to_psram();
    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    state_store_init();
    ESP_ERROR_CHECK(command_publisher_init());
    ESP_ERROR_CHECK(api_jobs_start());

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
        ui_init();
        ESP_ERROR_CHECK(ui_bridge_start());
        lvgl_port_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(100));  // primera imagen ya dibujada: recién ahí la luz
    ESP_ERROR_CHECK(hal_lcd_backlight(true));
    log_memory("interfaz lista");

    esp_err_t err2 = live_view_init();
    if (err2 != ESP_OK) {
        ESP_LOGE(TAG, "Vista en vivo no disponible: %s", esp_err_to_name(err2));
    }
    /* Antes que la red: así ve el primer status (retenido) de cada cámara */
    err2 = thumb_service_start();
    if (err2 != ESP_OK) {
        ESP_LOGE(TAG, "Miniaturas no disponibles: %s", esp_err_to_name(err2));
    }
    esp_err_t net = net_task_start();
    if (net != ESP_OK) {
        ESP_LOGE(TAG, "Red no iniciada (%s): la pantalla sigue sin datos", esp_err_to_name(net));
    }
}
