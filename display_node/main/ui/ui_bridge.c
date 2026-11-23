/**
 * @file ui_bridge.c
 * @brief Ver ui_bridge.h.
 */
#include "ui_bridge.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include "state_store.h"
#include "api_jobs.h"
#include "ui_api.h"

static const char *TAG = "ui_bridge";

#define QUEUE_LEN       32
#define DRAIN_PERIOD_MS 100
#define TICK_PERIOD_MS  1000   /**< refresco de "hace N min" y del reloj */

typedef struct {
    ss_change_t change;
    char node_id[SS_ID_LEN];
} ui_msg_t;

static QueueHandle_t s_q;
static volatile bool s_overflow;  /**< se perdió algún aviso: redibujar todo */

static void on_change(ss_change_t change, const char *node_id)
{
    ui_msg_t m = { .change = change };
    if (node_id) {
        strlcpy(m.node_id, node_id, sizeof(m.node_id));
    }
    if (xQueueSend(s_q, &m, 0) != pdTRUE) {
        s_overflow = true;
    }
}

static void drain(lv_timer_t *t)
{
    static uint32_t since_tick;
    api_jobs_dispatch();  // resultados de pedidos HTTP: sus callbacks tocan la interfaz
    uint32_t dirty = 0;
    ui_msg_t m;
    while (xQueueReceive(s_q, &m, 0) == pdTRUE) {
        dirty |= m.change == SS_CHANGE_NODE ? UI_DIRTY_NODES
               : m.change == SS_CHANGE_EVENT ? UI_DIRTY_EVENTS
               : m.change == SS_CHANGE_CONN ? UI_DIRTY_CONN
               : m.change == SS_CHANGE_THUMB ? UI_DIRTY_THUMB
               : m.change == SS_CHANGE_ENV ? UI_DIRTY_ENV : UI_DIRTY_CMD;
    }
    if (s_overflow) {
        s_overflow = false;
        dirty = UI_DIRTY_ALL;
    }
    since_tick += DRAIN_PERIOD_MS;
    if (since_tick >= TICK_PERIOD_MS) {
        since_tick = 0;
        dirty |= UI_DIRTY_TICK;
    }
    /* Varios avisos seguidos se resuelven en un solo refresco */
    if (dirty) {
        ui_refresh(dirty);
    }
}

esp_err_t ui_bridge_start(void)
{
    s_q = xQueueCreate(QUEUE_LEN, sizeof(ui_msg_t));
    if (!s_q) {
        return ESP_ERR_NO_MEM;
    }
    state_store_subscribe(on_change);
    lv_timer_create(drain, DRAIN_PERIOD_MS, NULL);
    ESP_LOGI(TAG, "Puente modelo → interfaz activo");
    return ESP_OK;
}
