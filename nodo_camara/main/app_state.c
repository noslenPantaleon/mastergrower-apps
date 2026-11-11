/**
 * @file app_state.c
 */
#include "app_state.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "processes/net_task.h"

static const char *TAG = "app_state";

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static mg_estado_nodo_t s_estado = MG_ESTADO_ARRANCANDO;
static time_t s_ultima_captura;

void app_state_init(void)
{
    s_estado = MG_ESTADO_ARRANCANDO;
    s_ultima_captura = 0;
}

void app_state_set_estado(mg_estado_nodo_t estado)
{
    taskENTER_CRITICAL(&s_mux);
    bool changed = s_estado != estado;
    s_estado = estado;
    taskEXIT_CRITICAL(&s_mux);

    if (changed) {
        ESP_LOGI(TAG, "estado_nodo → %s", mg_estado_nodo_str(estado));
        net_task_request_status();
    }
}

mg_estado_nodo_t app_state_get_estado(void)
{
    taskENTER_CRITICAL(&s_mux);
    mg_estado_nodo_t e = s_estado;
    taskEXIT_CRITICAL(&s_mux);
    return e;
}

void app_state_set_ultima_captura(time_t t)
{
    taskENTER_CRITICAL(&s_mux);
    s_ultima_captura = t;
    taskEXIT_CRITICAL(&s_mux);
    net_task_request_status();
}

time_t app_state_get_ultima_captura(void)
{
    taskENTER_CRITICAL(&s_mux);
    time_t t = s_ultima_captura;
    taskEXIT_CRITICAL(&s_mux);
    return t;
}
