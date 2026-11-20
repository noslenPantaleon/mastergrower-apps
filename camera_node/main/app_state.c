/**
 * @file app_state.c
 */
#include "app_state.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "processes/net_task.h"

static const char *TAG = "app_state";

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static mg_node_state_t s_state = MG_STATE_BOOTING;
static time_t s_last_capture;

void app_state_init(void)
{
    s_state = MG_STATE_BOOTING;
    s_last_capture = 0;
}

bool app_state_set_state(mg_node_state_t state)
{
    taskENTER_CRITICAL(&s_mux);
    bool changed = s_state != state;
    s_state = state;
    taskEXIT_CRITICAL(&s_mux);

    if (changed) {
        ESP_LOGI(TAG, "node_state → %s", mg_node_state_str(state));
        net_task_request_status();
    }
    return changed;
}

mg_node_state_t app_state_get_state(void)
{
    taskENTER_CRITICAL(&s_mux);
    mg_node_state_t e = s_state;
    taskEXIT_CRITICAL(&s_mux);
    return e;
}

void app_state_set_last_capture(time_t t)
{
    taskENTER_CRITICAL(&s_mux);
    s_last_capture = t;
    taskEXIT_CRITICAL(&s_mux);
    net_task_request_status();
}

time_t app_state_get_last_capture(void)
{
    taskENTER_CRITICAL(&s_mux);
    time_t t = s_last_capture;
    taskEXIT_CRITICAL(&s_mux);
    return t;
}
