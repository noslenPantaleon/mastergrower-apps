/**
 * @file config_sync_task.c
 * @brief Ver config_sync_task.h.
 */
#include "config_sync_task.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "capture_task.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mg_api.h"
#include "mg_ota.h"
#include "mg_wifi.h"
#include "net_task.h"
#include "system_config.h"

static const char *TAG = "config_sync";

#define MAX_BODY          2048
#define BACKOFF_MIN_S     10
#define MAX_PENDING_CMDS  4

static struct {
    TaskHandle_t task;
    portMUX_TYPE mux;
    char pending[MAX_PENDING_CMDS][MG_CMD_ID_MAX_LEN + 1];  /**< refresh_config esperando el ack final */
} s_cs = { .mux = portMUX_INITIALIZER_UNLOCKED };

/* ---------------------------------------------------------------- aplicar */

void config_sync_to_trigger(const node_config_t *c, trigger_config_t *out)
{
    trigger_config_default(out);  // base: si algo no se puede traducir queda el default
    out->capture_interval_min = c->capture_interval_min;
    out->utc_offset_min = c->utc_offset_min;
    out->min_interval_s = c->min_trigger_interval_s;
    uint16_t a, b;
    if (trigger_parse_hhmm(c->light_start, &a) && trigger_parse_hhmm(c->light_end, &b)) {
        out->light_start_min = a;
        out->light_end_min = b;
    }
    hal_camera_res_from_name(c->resolution, &out->res_routine);
    hal_camera_res_from_name(c->resolution_full, &out->res_full);
}

void config_sync_apply(void)
{
    node_config_t c;
    config_store_get(&c);
    trigger_config_t t;
    config_sync_to_trigger(&c, &t);
    if (trigger_manager_set_config(&t) != ESP_OK) {
        ESP_LOGW(TAG, "trigger_manager no iniciado: la config se aplica en el próximo arranque");
    }
    capture_task_set_quality(c.jpeg_quality);
    ESP_LOGI(TAG, "Config v%" PRIu32 " aplicada (análisis %s, jpeg q%u, umbral HSV %.2f)", c.version,
             c.analysis_mode, c.jpeg_quality, (double)c.hsv_suspicion_threshold);
    net_task_request_status();  // publica config_version
}

/* ---------------------------------------------------------------- refresh_config */

static void finish_pending(bool ok)
{
    char ids[MAX_PENDING_CMDS][MG_CMD_ID_MAX_LEN + 1];
    taskENTER_CRITICAL(&s_cs.mux);
    memcpy(ids, s_cs.pending, sizeof(ids));
    memset(s_cs.pending, 0, sizeof(s_cs.pending));
    taskEXIT_CRITICAL(&s_cs.mux);
    for (int i = 0; i < MAX_PENDING_CMDS; i++) {
        if (ids[i][0]) {
            net_task_send_ack(ids[i], ok ? MG_ACK_DONE : MG_ACK_REJECTED, ok ? NULL : "config_unavailable");
        }
    }
}

/** Corre en net_task: decide rápido y deja la consulta HTTP a esta tarea. */
static esp_err_t cmd_refresh_config(const mg_cmd_t *cmd, char *reason, size_t reason_len)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(cmd->params, "version");
    if (cJSON_IsNumber(v) && (uint32_t)v->valuedouble == config_store_version()) {
        snprintf(reason, reason_len, "already_current");
        return ESP_OK;  // ya está aplicada: accepted + done sin consultar
    }
    bool queued = false;
    taskENTER_CRITICAL(&s_cs.mux);
    for (int i = 0; i < MAX_PENDING_CMDS && !queued; i++) {
        if (!s_cs.pending[i][0]) {
            strlcpy(s_cs.pending[i], cmd->cmd_id, sizeof(s_cs.pending[i]));
            queued = true;
        }
    }
    taskEXIT_CRITICAL(&s_cs.mux);
    if (!queued) {
        snprintf(reason, reason_len, "queue_full");
        return ESP_ERR_NO_MEM;
    }
    xTaskNotifyGive(s_cs.task);
    return ESP_ERR_NOT_FINISHED;  // el "done" lo manda la tarea tras consultar
}

/* ---------------------------------------------------------------- tarea */

/** @return true si la API respondió (200 o 304). */
static bool sync_once(void)
{
    char path[48], etag[16];
    snprintf(path, sizeof(path), "/config/nodes/%s", SYS_NODE_ID);
    uint32_t cur = config_store_version();
    snprintf(etag, sizeof(etag), "\"%" PRIu32 "\"", cur);

    mg_api_resp_t resp;
    esp_err_t err = mg_api_get(path, etag, MAX_BODY, &resp);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "API sin respuesta (%s): sigue la config v%" PRIu32, esp_err_to_name(err), cur);
        mg_api_resp_free(&resp);
        return false;
    }
    bool ok = true;
    if (resp.status == 304 || resp.status == 200) {
        mg_ota_note_api_ok();  // la API responde: condición para confirmar un firmware nuevo
    }
    if (resp.status == 304) {
        ESP_LOGI(TAG, "Config v%" PRIu32 " al día", cur);
    } else if (resp.status == 200) {
        bool changed = false;
        if (config_store_apply_json(resp.body, resp.body_len, &changed) != ESP_OK) {
            ok = false;
        } else if (changed) {
            ESP_LOGI(TAG, "Config nueva: v%" PRIu32 " → v%" PRIu32, cur, config_store_version());
            config_sync_apply();
        }
    } else {
        ESP_LOGW(TAG, "GET %s → HTTP %d", path, resp.status);
        ok = false;
    }
    mg_api_resp_free(&resp);
    return ok;
}

static void config_sync_task(void *arg)
{
    const uint32_t period_s = CONFIG_MG_CONFIG_SYNC_PERIOD_S;
    uint32_t backoff_s = BACKOFF_MIN_S;
    for (;;) {
        mg_wifi_wait_connected(portMAX_DELAY);
        bool ok = sync_once();
        finish_pending(ok);

        uint32_t wait_s = period_s;
        if (ok) {
            backoff_s = BACKOFF_MIN_S;
        } else {
            /* ±20 % de azar: varios nodos no reintentan todos a la vez. */
            wait_s = backoff_s * (80 + esp_random() % 41) / 100;
            backoff_s = backoff_s * 2 > period_s ? period_s : backoff_s * 2;
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS((uint64_t)wait_s * 1000));  // refresh_config despierta antes
    }
}

esp_err_t config_sync_task_start(void)
{
    if (s_cs.task) {
        return ESP_ERR_INVALID_STATE;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(config_sync_task, "config_sync", SYS_CONFIG_SYNC_STACK, NULL,
                                            SYS_CONFIG_SYNC_PRIO, &s_cs.task, SYS_CORE_NET);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return net_task_register_cmd(MG_ACTION_REFRESH_CONFIG, cmd_refresh_config);
}
