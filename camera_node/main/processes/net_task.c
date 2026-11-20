/**
 * @file net_task.c
 * @brief Ver net_task.h. Los callbacks de MQTT y del event loop solo encolan; todo el trabajo
 *        (armar JSON, validar, ejecutar handlers) ocurre en esta tarea.
 */
#include "net_task.h"

#include <stdlib.h>
#include <string.h>
#include "app_state.h"
#include "config_store.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hal_camera.h"
#include "hal_storage.h"
#include "mg_mqtt.h"
#include "mg_time.h"
#include "mg_wifi.h"
#include "system_config.h"

static const char *TAG = "net_task";

#define NET_QUEUE_LEN       8
#define CMD_HISTORY_LEN     16
#define TIME_SYNC_WAIT_MS   10000

typedef enum {
    NET_MSG_STATUS,        /**< republicar status */
    NET_MSG_CONNECTED,
    NET_MSG_DISCONNECTED,
    NET_MSG_CMD,           /**< payload: JSON del comando (malloc) */
} net_msg_type_t;

typedef struct {
    net_msg_type_t type;
    char *payload;
    size_t len;
} net_msg_t;

static struct {
    QueueHandle_t q;
    TaskHandle_t task;
    net_cmd_handler_t handlers[MG_ACTION_SET_MODE + 1];
    char topic_status[MG_TOPIC_MAX_LEN];
    char topic_event[MG_TOPIC_MAX_LEN];
    char topic_cmd[MG_TOPIC_MAX_LEN];
    char topic_ack[MG_TOPIC_MAX_LEN];
    char *lwt_payload;
    char cmd_history[CMD_HISTORY_LEN][MG_CMD_ID_MAX_LEN + 1];
    int cmd_history_next;
} s_net;

/* ---------------------------------------------------------------- encolado */

static void post(net_msg_type_t type, char *payload, size_t len)
{
    if (!s_net.q) {
        free(payload);
        return;
    }
    net_msg_t m = { .type = type, .payload = payload, .len = len };
    if (xQueueSend(s_net.q, &m, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Cola llena: se descarta mensaje tipo %d", type);
        free(payload);
    }
}

void net_task_request_status(void)
{
    post(NET_MSG_STATUS, NULL, 0);
}

static void on_cmd_msg(const mg_mqtt_msg_t *msg, void *ctx)
{
    char *copy = malloc(msg->data_len + 1);
    if (!copy) {
        return;
    }
    memcpy(copy, msg->data, msg->data_len);
    copy[msg->data_len] = '\0';
    post(NET_MSG_CMD, copy, msg->data_len);
}

static void on_mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    post(id == MG_MQTT_EVENT_CONNECTED ? NET_MSG_CONNECTED : NET_MSG_DISCONNECTED, NULL, 0);
}

static void on_time_synced(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    post(NET_MSG_STATUS, NULL, 0);  // republicar con ts y last_capture correctos
}

/* ---------------------------------------------------------------- publicación */

static void publish_status(void)
{
    if (!mg_mqtt_is_connected()) {
        return;
    }
    char last[MG_TIME_ISO_LEN];
    time_t t = app_state_get_last_capture();
    if (t) {
        mg_time_to_iso(t, last, sizeof(last));
    }
    const mg_cam_status_t st = {
        .node_state = app_state_get_state(),
        .camera_type = hal_camera_type(),
        .firmware = SYS_FIRMWARE_VERSION,
        .last_capture = t ? last : NULL,
        .sd_free_pct = hal_storage_free_pct(),  // -1 → null si no hay tarjeta
        .rssi = mg_wifi_get_rssi(),
        .zone_id = NULL,     // la zona la asigna la API (B4); el nodo no la conoce todavía
        .has_config_version = true,
        .config_version = config_store_version(),
    };
    char *json = mg_msg_cam_status(SYS_NODE_ID, &st);
    if (json) {
        mg_mqtt_publish(s_net.topic_status, json, -1, 1, true);
        ESP_LOGI(TAG, "status → %s", json);
        free(json);
    }
}

esp_err_t net_task_send_ack(const char *cmd_id, mg_ack_result_t res, const char *reason)
{
    char *json = mg_msg_cmd_ack(SYS_NODE_ID, cmd_id, res, reason);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    int id = mg_mqtt_publish(s_net.topic_ack, json, -1, 1, false);
    ESP_LOGI(TAG, "ack → %s", json);
    free(json);
    return id >= 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t net_task_publish_event(mg_event_type_t type, mg_severity_t sev, const char *capture_id,
                                 const char *detail)
{
    char *json = mg_msg_cam_event(SYS_NODE_ID, type, sev, capture_id, detail);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    int id = mg_mqtt_publish(s_net.topic_event, json, -1, 1, false);
    ESP_LOGI(TAG, "event → %s", json);
    free(json);
    return id >= 0 ? ESP_OK : ESP_FAIL;
}

/* ---------------------------------------------------------------- comandos */

static bool cmd_seen(const char *cmd_id)
{
    for (int i = 0; i < CMD_HISTORY_LEN; i++) {
        if (strcmp(s_net.cmd_history[i], cmd_id) == 0) {
            return true;
        }
    }
    return false;
}

static void cmd_remember(const char *cmd_id)
{
    strlcpy(s_net.cmd_history[s_net.cmd_history_next], cmd_id, sizeof(s_net.cmd_history[0]));
    s_net.cmd_history_next = (s_net.cmd_history_next + 1) % CMD_HISTORY_LEN;
}

static void handle_cmd(const char *json, size_t len)
{
    mg_cmd_t cmd;
    esp_err_t err = mg_cmd_parse(json, len, &cmd);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Comando inválido (%s): %.*s", esp_err_to_name(err), (int)len, json);
        if (cmd.cmd_id[0]) {
            net_task_send_ack(cmd.cmd_id, MG_ACK_REJECTED,
                              err == ESP_ERR_NOT_SUPPORTED ? "unsupported_schema_version" : "invalid_format");
        }
        mg_cmd_free(&cmd);
        return;
    }

    if (cmd_seen(cmd.cmd_id)) {
        /* QoS 1 puede reentregar: un cmd_id ya procesado se descarta sin volver a ejecutarlo. */
        ESP_LOGW(TAG, "cmd_id %s repetido: descartado", cmd.cmd_id);
        mg_cmd_free(&cmd);
        return;
    }
    cmd_remember(cmd.cmd_id);

    ESP_LOGI(TAG, "cmd %s (cmd_id=%s, de %s)", cmd.action_raw, cmd.cmd_id, cmd.sender[0] ? cmd.sender : "?");

    /* Vigencia: solo se puede verificar si ambos relojes son válidos. */
    if (cmd.ts && mg_time_is_valid()) {
        double age = difftime(time(NULL), cmd.ts);
        if (age > MG_CMD_VALIDITY_S || age < -MG_CMD_VALIDITY_S) {
            net_task_send_ack(cmd.cmd_id, MG_ACK_REJECTED, "expired");
            mg_cmd_free(&cmd);
            return;
        }
    } else {
        ESP_LOGW(TAG, "No se puede verificar la vigencia de %s (sin ts o sin hora)", cmd.cmd_id);
    }

    net_cmd_handler_t h = s_net.handlers[cmd.action];
    if (cmd.action == MG_ACTION_UNKNOWN || !h) {
        net_task_send_ack(cmd.cmd_id, MG_ACK_REJECTED, "unsupported_action");
        mg_cmd_free(&cmd);
        return;
    }

    /* El handler decide primero (debe ser breve): así un rechazo nunca va precedido de "accepted". */
    char reason[64] = "";
    err = h(&cmd, reason, sizeof(reason));
    if (err == ESP_OK || err == ESP_ERR_NOT_FINISHED) {
        net_task_send_ack(cmd.cmd_id, MG_ACK_ACCEPTED, NULL);
        if (err == ESP_OK) {
            net_task_send_ack(cmd.cmd_id, MG_ACK_DONE, reason[0] ? reason : NULL);
        }
    } else {
        net_task_send_ack(cmd.cmd_id, MG_ACK_REJECTED, reason[0] ? reason : esp_err_to_name(err));
    }
    mg_cmd_free(&cmd);
}

esp_err_t net_task_register_cmd(mg_action_t action, net_cmd_handler_t handler)
{
    if (action == MG_ACTION_UNKNOWN || action > MG_ACTION_SET_MODE || !handler) {
        return ESP_ERR_INVALID_ARG;
    }
    s_net.handlers[action] = handler;
    return ESP_OK;
}

/* ---------------------------------------------------------------- tarea */

static void start_mqtt(void)
{
    /* LWT: lo publica el broker si el nodo desaparece sin desconectarse. */
    mg_cam_status_t lwt = {
        .node_state = MG_STATE_OFFLINE,
        .camera_type = hal_camera_type(),
        .firmware = SYS_FIRMWARE_VERSION,
        .sd_free_pct = -1,
    };
    s_net.lwt_payload = mg_msg_cam_status(SYS_NODE_ID, &lwt);

    mg_mqtt_subscribe(s_net.topic_cmd, 1, on_cmd_msg, NULL);
    esp_event_handler_register(MG_MQTT_EVENT, ESP_EVENT_ANY_ID, on_mqtt_event, NULL);

    const mg_mqtt_config_t cfg = {
        .client_id = SYS_NODE_ID,
        /* Usuario MQTT = node_id salvo que se configure otro (las ACL del broker usan %u). */
        .username = CONFIG_MG_MQTT_USERNAME[0] ? NULL : SYS_NODE_ID,
        .lwt_topic = s_net.topic_status,
        .lwt_msg = s_net.lwt_payload,
        .lwt_qos = 1,
        .lwt_retain = true,
    };
    ESP_ERROR_CHECK(mg_mqtt_start(&cfg));
}

static void net_task(void *arg)
{
    /* La hora hace falta para los ts de los mensajes y la vigencia de los comandos. */
    mg_wifi_wait_connected(portMAX_DELAY);
    esp_event_handler_register(MG_TIME_EVENT, MG_TIME_EVENT_SYNCED, on_time_synced, NULL);
    mg_time_start(NULL);
    if (mg_time_wait_sync(pdMS_TO_TICKS(TIME_SYNC_WAIT_MS)) != ESP_OK) {
        ESP_LOGW(TAG, "Sin hora NTP todavía: se publica igual y se corrige al sincronizar");
    }
    start_mqtt();

    const TickType_t period = pdMS_TO_TICKS(CONFIG_MG_STATUS_PERIOD_S * 1000);
    net_msg_t m;
    for (;;) {
        if (xQueueReceive(s_net.q, &m, period) != pdTRUE) {
            publish_status();  // latido periódico (rssi, etc.)
            continue;
        }
        switch (m.type) {
        case NET_MSG_CONNECTED:
            /* Siempre se publica al conectar (pisa el LWT retenido). Si el estado cambia,
             * app_state ya encoló la publicación; si no, se publica acá. */
            if (app_state_get_state() == MG_STATE_ERROR ||  // un error de hardware no se borra por reconectar
                !app_state_set_state(MG_STATE_OPERATIONAL)) {
                publish_status();
            }
            break;
        case NET_MSG_DISCONNECTED:
            if (app_state_get_state() != MG_STATE_ERROR) {
                app_state_set_state(MG_STATE_AUTONOMOUS);
            }
            break;
        case NET_MSG_STATUS:
            publish_status();
            break;
        case NET_MSG_CMD:
            handle_cmd(m.payload, m.len);
            break;
        }
        free(m.payload);
    }
}

esp_err_t net_task_start(void)
{
    if (s_net.task) {
        return ESP_ERR_INVALID_STATE;
    }
    mg_topic(s_net.topic_status, MG_TOPIC_MAX_LEN, MG_NODE_CAM, SYS_NODE_ID, MG_CH_STATUS);
    mg_topic(s_net.topic_event, MG_TOPIC_MAX_LEN, MG_NODE_CAM, SYS_NODE_ID, MG_CH_EVENT);
    mg_topic(s_net.topic_cmd, MG_TOPIC_MAX_LEN, MG_NODE_CAM, SYS_NODE_ID, MG_CH_CMD);
    mg_topic(s_net.topic_ack, MG_TOPIC_MAX_LEN, MG_NODE_CAM, SYS_NODE_ID, MG_CH_CMD_ACK);

    s_net.q = xQueueCreate(NET_QUEUE_LEN, sizeof(net_msg_t));
    if (!s_net.q) {
        return ESP_ERR_NO_MEM;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(net_task, "net_task", SYS_NET_TASK_STACK, NULL, SYS_NET_TASK_PRIO,
                                            &s_net.task, SYS_CORE_NET);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
