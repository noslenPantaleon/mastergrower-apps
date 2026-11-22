/**
 * @file command_publisher.c
 * @brief Ver command_publisher.h.
 */
#include "command_publisher.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mg_contract.h"
#include "mg_mqtt.h"
#include "mg_time.h"
#include "state_store.h"
#include "system_config.h"

static const char *TAG = "cmd_pub";

#define ACCEPT_TIMEOUT_US  (5LL * 1000000)
#define DONE_TIMEOUT_US    (20LL * 1000000)

typedef struct {
    char node_id[SS_ID_LEN];
    char cmd_id[MG_CMD_ID_MAX_LEN + 1];
    cmd_status_t st;
    int64_t sent_us;
} pending_t;

static struct {
    SemaphoreHandle_t lock;
    pending_t slots[SS_MAX_NODES];
    uint32_t seq;
} s_cp;

static pending_t *slot_for(const char *node_id, bool create)
{
    pending_t *free_slot = NULL;
    for (int i = 0; i < SS_MAX_NODES; i++) {
        if (strcmp(s_cp.slots[i].node_id, node_id) == 0) {
            return &s_cp.slots[i];
        }
        if (!free_slot && !s_cp.slots[i].node_id[0]) {
            free_slot = &s_cp.slots[i];
        }
    }
    if (create && free_slot) {
        memset(free_slot, 0, sizeof(*free_slot));
        strlcpy(free_slot->node_id, node_id, sizeof(free_slot->node_id));
    }
    return create ? free_slot : NULL;
}

static void set_state(pending_t *p, cmd_state_t st, const char *reason)
{
    p->st.state = st;
    strlcpy(p->st.reason, reason ? reason : "", sizeof(p->st.reason));
    p->st.updated_us = esp_timer_get_time();
}

esp_err_t command_publisher_init(void)
{
    s_cp.lock = xSemaphoreCreateMutex();
    return s_cp.lock ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t command_capture_now(const char *node_id)
{
    if (!mg_time_is_valid()) {
        return ESP_ERR_INVALID_STATE;
    }
    char topic[MG_TOPIC_MAX_LEN];
    if (mg_topic(topic, sizeof(topic), MG_NODE_CAM, node_id, MG_CH_CMD) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_cp.lock, portMAX_DELAY);
    pending_t *p = slot_for(node_id, true);
    if (!p || p->st.state == CMD_SENT || p->st.state == CMD_ACCEPTED) {
        xSemaphoreGive(s_cp.lock);
        return ESP_ERR_INVALID_STATE;
    }
    snprintf(p->cmd_id, sizeof(p->cmd_id), "%s-%" PRIu32 "-%04" PRIx32, SYS_NODE_ID, ++s_cp.seq,
             esp_random() & 0xffff);
    char cmd_id[sizeof(p->cmd_id)];
    strlcpy(cmd_id, p->cmd_id, sizeof(cmd_id));
    xSemaphoreGive(s_cp.lock);

    cJSON *o = mg_msg_new(SYS_NODE_ID);
    if (!o) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(o, "action", mg_action_str(MG_ACTION_CAPTURE_NOW));
    cJSON_AddStringToObject(o, "cmd_id", cmd_id);
    char *msg = mg_msg_to_string(o);
    int id = msg ? mg_mqtt_publish(topic, msg, -1, 1, false) : -1;
    free(msg);

    xSemaphoreTake(s_cp.lock, portMAX_DELAY);
    p->sent_us = esp_timer_get_time();
    set_state(p, id >= 0 ? CMD_SENT : CMD_REJECTED, id >= 0 ? NULL : "sin_conexion");
    xSemaphoreGive(s_cp.lock);
    state_store_notify(SS_CHANGE_CMD, node_id);
    ESP_LOGI(TAG, "capture_now → %s (%s)%s", node_id, cmd_id, id >= 0 ? "" : ": sin conexión");
    return id >= 0 ? ESP_OK : ESP_FAIL;
}

cmd_status_t command_status(const char *node_id)
{
    cmd_status_t st = { .state = CMD_IDLE };
    xSemaphoreTake(s_cp.lock, portMAX_DELAY);
    pending_t *p = slot_for(node_id, false);
    if (p) {
        st = p->st;
    }
    xSemaphoreGive(s_cp.lock);
    return st;
}

void command_publisher_on_ack(const char *node_id, const char *json, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        return;
    }
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "cmd_id");
    const cJSON *res = cJSON_GetObjectItemCaseSensitive(root, "result");
    const cJSON *why = cJSON_GetObjectItemCaseSensitive(root, "reason");
    bool changed = false;
    if (cJSON_IsString(id) && cJSON_IsString(res)) {
        xSemaphoreTake(s_cp.lock, portMAX_DELAY);
        pending_t *p = slot_for(node_id, false);
        /* Solo los acks de comandos propios y en curso (los de la API u otra pantalla no) */
        if (p && strcmp(p->cmd_id, id->valuestring) == 0 &&
            (p->st.state == CMD_SENT || p->st.state == CMD_ACCEPTED)) {
            const char *reason = cJSON_IsString(why) ? why->valuestring : NULL;
            if (strcmp(res->valuestring, "accepted") == 0) {
                set_state(p, CMD_ACCEPTED, NULL);
            } else if (strcmp(res->valuestring, "done") == 0) {
                set_state(p, CMD_DONE, reason);
            } else {
                set_state(p, CMD_REJECTED, reason ? reason : "rejected");
            }
            changed = true;
        }
        xSemaphoreGive(s_cp.lock);
    }
    cJSON_Delete(root);
    if (changed) {
        state_store_notify(SS_CHANGE_CMD, node_id);
    }
}

void command_publisher_tick(void)
{
    int64_t now = esp_timer_get_time();
    char expired[SS_MAX_NODES][SS_ID_LEN];
    int n = 0;
    xSemaphoreTake(s_cp.lock, portMAX_DELAY);
    for (int i = 0; i < SS_MAX_NODES; i++) {
        pending_t *p = &s_cp.slots[i];
        if ((p->st.state == CMD_SENT && now - p->sent_us > ACCEPT_TIMEOUT_US) ||
            (p->st.state == CMD_ACCEPTED && now - p->sent_us > DONE_TIMEOUT_US)) {
            set_state(p, CMD_TIMEOUT, NULL);
            strlcpy(expired[n++], p->node_id, SS_ID_LEN);
        }
    }
    xSemaphoreGive(s_cp.lock);
    for (int i = 0; i < n; i++) {
        ESP_LOGW(TAG, "Sin respuesta de %s", expired[i]);
        state_store_notify(SS_CHANGE_CMD, expired[i]);
    }
}
