/**
 * @file trigger_manager.c
 * @brief Ver trigger_manager.h. Las reglas se aplican en trigger_request() (en el contexto de
 *        quien pide), así un comando remoto recibe el rechazo de forma inmediata. La tarea
 *        propia solo genera los disparos del timer.
 */
#include "trigger_manager.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "default_params.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mg_time.h"
#include "system_config.h"

static const char *TAG = "trigger_mgr";

static struct {
    SemaphoreHandle_t lock;
    TaskHandle_t task;
    trigger_config_t cfg;
    int64_t last_accepted_us; /**< 0 = nunca */
    volatile bool streaming;
    volatile bool timer_deferred; /**< un disparo del timer llegó durante el stream */
} s_tm;

#define NOTIFY_CONFIG      1
#define NOTIFY_STREAM_END  2

bool trigger_parse_hhmm(const char *s, uint16_t *out_min)
{
    unsigned h, m;
    char extra;
    if (!s || sscanf(s, "%2u:%2u%c", &h, &m, &extra) != 2 || h > 24 || m > 59 || (h == 24 && m != 0)) {
        return false;
    }
    *out_min = (uint16_t)((h % 24) * 60 + m);  // "24:00" equivale a "00:00"
    return true;
}

void trigger_config_default(trigger_config_t *cfg)
{
    *cfg = (trigger_config_t){
        .capture_interval_min = DEF_CAPTURE_INTERVAL_MIN,
        .utc_offset_min = DEF_UTC_OFFSET_MIN,
        .min_interval_s = DEF_MIN_TRIGGER_INTERVAL_S,
        .res_routine = HAL_CAM_RES_SVGA,
        .res_full = HAL_CAM_RES_UXGA,
    };
    if (!trigger_parse_hhmm(DEF_LIGHT_WINDOW_START, &cfg->light_start_min) ||
        !trigger_parse_hhmm(DEF_LIGHT_WINDOW_END, &cfg->light_end_min)) {
        ESP_LOGW(TAG, "Ventana de luz por defecto inválida: se usa todo el día");
        cfg->light_start_min = cfg->light_end_min = 0;
    }
    if (!hal_camera_res_from_name(DEF_RESOLUTION, &cfg->res_routine)) {
        ESP_LOGW(TAG, "Resolución de rutina \"%s\" inválida", DEF_RESOLUTION);
    }
    if (!hal_camera_res_from_name(DEF_RESOLUTION_FULL, &cfg->res_full)) {
        ESP_LOGW(TAG, "Resolución completa \"%s\" inválida", DEF_RESOLUTION_FULL);
    }
}

static bool in_window(uint16_t start, uint16_t end, uint16_t m)
{
    if (start == end) {
        return true;  // ventana de 24 h
    }
    if (start < end) {
        return m >= start && m < end;
    }
    return m >= start || m < end;  // cruza medianoche (p. ej. 06:00 → 00:00)
}

bool trigger_manager_light_window_active(void)
{
    if (!mg_time_is_valid()) {
        return true;  // sin hora no se puede saber: se prioriza seguir capturando
    }
    xSemaphoreTake(s_tm.lock, portMAX_DELAY);
    trigger_config_t c = s_tm.cfg;
    xSemaphoreGive(s_tm.lock);

    int64_t local_s = (int64_t)time(NULL) + (int64_t)c.utc_offset_min * 60;
    uint16_t minute_of_day = (uint16_t)(((local_s % 86400) + 86400) % 86400 / 60);
    return in_window(c.light_start_min, c.light_end_min, minute_of_day);
}

static priority_t priority_of(mg_trigger_type_t t)
{
    return (t == MG_TRIGGER_TIMER || t == MG_TRIGGER_SCHEDULE) ? PRIORITY_NORMAL : PRIORITY_HIGH;
}

esp_err_t trigger_request(const trigger_req_t *req, const char **reason)
{
    const char *dummy;
    if (!reason) {
        reason = &dummy;
    }
    *reason = NULL;
    if (!req || !s_tm.lock) {
        *reason = "not_initialized";
        return ESP_ERR_INVALID_STATE;
    }

    if (req->type == MG_TRIGGER_TIMER && !trigger_manager_light_window_active()) {
        *reason = "outside_light_window";
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_tm.lock, portMAX_DELAY);
    int64_t now = esp_timer_get_time();
    if (s_tm.last_accepted_us &&
        now - s_tm.last_accepted_us < (int64_t)s_tm.cfg.min_interval_s * 1000000) {
        xSemaphoreGive(s_tm.lock);
        *reason = "min_interval";
        return ESP_ERR_INVALID_STATE;
    }

    trigger_t t = {
        .type = req->type,
        .priority = priority_of(req->type),
        .requested_us = now,
    };
    t.resolution = t.priority == PRIORITY_HIGH ? s_tm.cfg.res_full : s_tm.cfg.res_routine;
    strlcpy(t.cmd_id, req->cmd_id, sizeof(t.cmd_id));

    /* Prioridad alta adelante de la cola: no espera detrás de capturas de rutina. */
    BaseType_t ok = t.priority == PRIORITY_HIGH ? xQueueSendToFront(q_trigger, &t, 0)
                                                  : xQueueSendToBack(q_trigger, &t, 0);
    if (ok == pdTRUE) {
        s_tm.last_accepted_us = now;
    }
    xSemaphoreGive(s_tm.lock);

    if (ok != pdTRUE) {
        *reason = "queue_full";
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "disparo %s aceptado (prioridad %s, %s)", mg_trigger_type_str(t.type),
             t.priority == PRIORITY_HIGH ? "high" : "normal", hal_camera_res_name(t.resolution));
    return ESP_OK;
}

static TickType_t timer_period(void)
{
    xSemaphoreTake(s_tm.lock, portMAX_DELAY);
    uint32_t min = s_tm.cfg.capture_interval_min;
    xSemaphoreGive(s_tm.lock);
    return pdMS_TO_TICKS((uint64_t)(min ? min : 1) * 60 * 1000);
}

static void trigger_task(void *arg)
{
    TickType_t next = xTaskGetTickCount() + timer_period();
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        TickType_t wait = (int32_t)(next - now) > 0 ? next - now : 0;

        uint32_t why;
        if (xTaskNotifyWait(0, UINT32_MAX, &why, wait) == pdTRUE) {
            if (why == NOTIFY_CONFIG) {
                next = xTaskGetTickCount() + timer_period();  // cambió la config: reiniciar conteo
                continue;
            }
            /* NOTIFY_STREAM_END: el disparo diferido sale ahora; el conteo sigue igual */
            if (!s_tm.timer_deferred) {
                continue;
            }
            ESP_LOGI(TAG, "fin del stream: sale el disparo de timer diferido");
        } else {
            next += timer_period();
        }

        if (s_tm.streaming) {
            s_tm.timer_deferred = true;
            ESP_LOGI(TAG, "disparo de timer diferido: vista en vivo activa");
            continue;
        }
        s_tm.timer_deferred = false;

        const char *reason;
        const trigger_req_t req = { .type = MG_TRIGGER_TIMER };
        if (trigger_request(&req, &reason) != ESP_OK) {
            ESP_LOGI(TAG, "disparo de timer omitido: %s", reason);
        }
    }
}

void trigger_manager_set_streaming(bool streaming)
{
    s_tm.streaming = streaming;
    if (!streaming && s_tm.timer_deferred && s_tm.task) {
        xTaskNotify(s_tm.task, NOTIFY_STREAM_END, eSetValueWithoutOverwrite);
    }
}

esp_err_t trigger_manager_set_config(const trigger_config_t *cfg)
{
    if (!cfg || !s_tm.lock) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_tm.lock, portMAX_DELAY);
    s_tm.cfg = *cfg;
    xSemaphoreGive(s_tm.lock);
    if (s_tm.task) {
        xTaskNotify(s_tm.task, NOTIFY_CONFIG, eSetValueWithOverwrite);
    }
    ESP_LOGI(TAG, "config: cada %" PRIu32 " min, luz %02u:%02u–%02u:%02u (UTC%+d min), rutina %s, completa %s, "
             "mín %us", cfg->capture_interval_min, cfg->light_start_min / 60, cfg->light_start_min % 60,
             cfg->light_end_min / 60, cfg->light_end_min % 60, cfg->utc_offset_min,
             hal_camera_res_name(cfg->res_routine), hal_camera_res_name(cfg->res_full), cfg->min_interval_s);
    return ESP_OK;
}

esp_err_t trigger_manager_start(const trigger_config_t *cfg)
{
    if (s_tm.task || !q_trigger) {
        return ESP_ERR_INVALID_STATE;
    }
    s_tm.lock = xSemaphoreCreateMutex();
    if (!s_tm.lock) {
        return ESP_ERR_NO_MEM;
    }
    trigger_manager_set_config(cfg);
    BaseType_t ok = xTaskCreatePinnedToCore(trigger_task, "trigger_mgr", SYS_TRIGGER_STACK, NULL,
                                            SYS_TRIGGER_PRIO, &s_tm.task, SYS_CORE_APP);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
