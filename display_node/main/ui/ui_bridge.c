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
#include "ui_status.h"

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
    bool nodes = false, events = false, conn = false;
    ui_msg_t m;
    while (xQueueReceive(s_q, &m, 0) == pdTRUE) {
        nodes |= m.change == SS_CHANGE_NODE;
        events |= m.change == SS_CHANGE_EVENT;
        conn |= m.change == SS_CHANGE_CONN;
    }
    if (s_overflow) {
        s_overflow = false;
        nodes = events = conn = true;
    }
    since_tick += DRAIN_PERIOD_MS;
    bool tick = since_tick >= TICK_PERIOD_MS;
    if (tick) {
        since_tick = 0;
    }
    /* Varios avisos seguidos se resuelven en un solo redibujado */
    if (conn || tick) {
        ui_status_refresh_conn();
    }
    if (nodes || tick) {
        ui_status_refresh_nodes();
    }
    if (events) {
        ui_status_refresh_events();
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
