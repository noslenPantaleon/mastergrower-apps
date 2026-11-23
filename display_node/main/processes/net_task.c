/**
 * @file net_task.c
 * @brief Ver net_task.h.
 */
#include "net_task.h"

#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "command_publisher.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mg_contract.h"
#include "mg_mqtt.h"
#include "mg_ota.h"
#include "mg_time.h"
#include "mg_wifi.h"
#include "state_store.h"
#include "system_config.h"

static const char *TAG = "net_task";

#define RX_QUEUE_LEN   32
#define TOPIC_MAX      64
#define STATUS_PERIOD_MS (300 * 1000)

typedef struct {
    char topic[TOPIC_MAX];
    char *data;   /**< PSRAM; lo libera la tarea */
    size_t len;
} rx_msg_t;

#define SEEN_CMDS  8

static struct {
    QueueHandle_t rx;
    char topic_status[MG_TOPIC_MAX_LEN];
    char topic_cmd[MG_TOPIC_MAX_LEN];
    char topic_ack[MG_TOPIC_MAX_LEN];
    char seen[SEEN_CMDS][MG_CMD_ID_MAX_LEN + 1];  /**< últimos cmd_id (descarta repetidos) */
    int seen_next;
    char *lwt;
    volatile bool status_due;
} s_net;

/* ---------------------------------------------------------------- MQTT → cola */

static void on_mqtt_msg(const mg_mqtt_msg_t *m, void *ctx)
{
    if (m->topic_len >= TOPIC_MAX || m->data_len == 0) {
        return;
    }
    rx_msg_t r = { .len = m->data_len };
    memcpy(r.topic, m->topic, m->topic_len);
    r.topic[m->topic_len] = '\0';
    r.data = heap_caps_malloc(m->data_len + 1, MALLOC_CAP_SPIRAM);
    if (!r.data) {
        return;
    }
    memcpy(r.data, m->data, m->data_len);
    r.data[m->data_len] = '\0';
    if (xQueueSend(s_net.rx, &r, 0) != pdTRUE) {
        heap_caps_free(r.data);
        ESP_LOGW(TAG, "Cola de mensajes llena: se descarta %s", r.topic);
    }
}

/** growlab/{kind}/{id}/{canal} → kind, id, canal. */
static bool split_topic(char *topic, mg_node_kind_t *kind, char **id, char **channel)
{
    char *save, *p = strtok_r(topic, "/", &save);
    if (!p || strcmp(p, "growlab") != 0 || !(p = strtok_r(NULL, "/", &save))) {
        return false;
    }
    if (strcmp(p, "cam") == 0) {
        *kind = MG_NODE_CAM;
    } else if (strcmp(p, "ctrl") == 0) {
        *kind = MG_NODE_CTRL;
    } else if (strcmp(p, "disp") == 0) {
        *kind = MG_NODE_DISP;
    } else {
        return false;
    }
    *id = strtok_r(NULL, "/", &save);
    *channel = save && *save ? save : NULL;  // el resto ("status", "cmd/ack", ...)
    return *id && *channel;
}

void net_task_send_ack(const char *cmd_id, mg_ack_result_t res, const char *reason)
{
    char *msg = mg_msg_cmd_ack(SYS_NODE_ID, cmd_id, res, reason);
    if (msg) {
        mg_mqtt_publish(s_net.topic_ack, msg, -1, 1, false);
        free(msg);
    }
    ESP_LOGI(TAG, "ack %s %s%s%s", cmd_id, mg_ack_result_str(res), reason ? " " : "", reason ? reason : "");
}

void net_task_request_status(void)
{
    s_net.status_due = true;
}

/** Comandos a la pantalla (hoy solo ota_update). Mismas reglas que la cámara: vigencia y repetidos. */
static void handle_cmd(const char *json, size_t len)
{
    mg_cmd_t cmd;
    esp_err_t err = mg_cmd_parse(json, len, &cmd);
    if (err != ESP_OK) {
        if (cmd.cmd_id[0]) {
            net_task_send_ack(cmd.cmd_id, MG_ACK_REJECTED,
                              err == ESP_ERR_NOT_SUPPORTED ? "unsupported_schema_version" : "invalid_format");
        }
        mg_cmd_free(&cmd);
        return;
    }
    for (int i = 0; i < SEEN_CMDS; i++) {
        if (strcmp(s_net.seen[i], cmd.cmd_id) == 0) {
            ESP_LOGW(TAG, "cmd %s repetido: se ignora", cmd.cmd_id);
            mg_cmd_free(&cmd);
            return;
        }
    }
    strlcpy(s_net.seen[s_net.seen_next], cmd.cmd_id, sizeof(s_net.seen[0]));
    s_net.seen_next = (s_net.seen_next + 1) % SEEN_CMDS;
    if (cmd.ts && mg_time_is_valid() && time(NULL) - cmd.ts > MG_CMD_VALIDITY_S) {
        net_task_send_ack(cmd.cmd_id, MG_ACK_REJECTED, "expired");
        mg_cmd_free(&cmd);
        return;
    }
    ESP_LOGI(TAG, "cmd %s (cmd_id=%s, de %s)", cmd.action_raw, cmd.cmd_id, cmd.sender);
    if (cmd.action == MG_ACTION_OTA_UPDATE) {
        mg_ota_req_t req = {
            .cmd_id = cmd.cmd_id,
            .url = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(cmd.params, "url")),
            .version = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(cmd.params, "version")),
            .sha256 = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(cmd.params, "sha256")),
        };
        char reason[32] = "";
        err = mg_ota_start(&req, reason, sizeof(reason));
        net_task_send_ack(cmd.cmd_id, err == ESP_OK ? MG_ACK_ACCEPTED : MG_ACK_REJECTED,
                          err == ESP_OK ? NULL : reason);
    } else {
        net_task_send_ack(cmd.cmd_id, MG_ACK_REJECTED, "unsupported_action");
    }
    mg_cmd_free(&cmd);
}

static void handle(rx_msg_t *r)
{
    if (strcmp(r->topic, s_net.topic_cmd) == 0) {
        handle_cmd(r->data, r->len);
        return;
    }
    mg_node_kind_t kind;
    char *id, *channel;
    if (!split_topic(r->topic, &kind, &id, &channel) || strlen(id) >= SS_ID_LEN) {
        return;
    }
    bool ok = true;
    if (strcmp(channel, MG_CH_STATUS) == 0 || strcmp(channel, MG_CH_STATE) == 0) {
        ok = state_store_apply_status(kind, id, r->data, r->len);
    } else if (strcmp(channel, MG_CH_EVENT) == 0) {
        ok = state_store_apply_event(id, r->data, r->len);
    } else if (strcmp(channel, MG_CH_CMD_ACK) == 0) {
        command_publisher_on_ack(id, r->data, r->len);
    }
    if (!ok) {
        ESP_LOGW(TAG, "Mensaje inválido de %s/%s", id, channel);
    }
}

/* ---------------------------------------------------------------- status propio */

static char *build_status(mg_node_state_t state)
{
    cJSON *o = mg_msg_new(SYS_NODE_ID);
    if (!o) {
        return NULL;
    }
    cJSON_AddStringToObject(o, "node_state", mg_node_state_str(state));
    cJSON_AddStringToObject(o, "firmware", SYS_FIRMWARE_VERSION);
    if (state != MG_STATE_OFFLINE && mg_wifi_is_connected()) {
        cJSON_AddNumberToObject(o, "rssi", mg_wifi_get_rssi());
    }
    return mg_msg_to_string(o);
}

static void publish_status(void)
{
    char *msg = build_status(MG_STATE_OPERATIONAL);
    if (msg) {
        mg_mqtt_publish(s_net.topic_status, msg, -1, 1, true);
        free(msg);
    }
}

static void update_conn(void)
{
    ss_conn_t c = { .wifi = mg_wifi_is_connected(), .mqtt = mg_mqtt_is_connected(),
                    .time_valid = mg_time_is_valid() };
    state_store_set_conn(&c);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == MG_MQTT_EVENT && id == MG_MQTT_EVENT_CONNECTED) {
        s_net.status_due = true;
        mg_ota_note_mqtt_ok();  // condición para confirmar un firmware nuevo
    }
    update_conn();
}

/* ---------------------------------------------------------------- tarea */

static void log_heap(void)
{
    ESP_LOGI(TAG, "memoria: interna libre %u KB (mín %u, bloque %u) · PSRAM libre %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

static void net_task(void *arg)
{
    TickType_t last_status = xTaskGetTickCount();
    TickType_t last_heap = last_status;
    for (;;) {
        rx_msg_t r;
        if (xQueueReceive(s_net.rx, &r, pdMS_TO_TICKS(1000)) == pdTRUE) {
            handle(&r);
            heap_caps_free(r.data);
        }
        if (s_net.status_due || xTaskGetTickCount() - last_status > pdMS_TO_TICKS(STATUS_PERIOD_MS)) {
            if (mg_mqtt_is_connected()) {
                publish_status();
                s_net.status_due = false;
                last_status = xTaskGetTickCount();
            }
        }
        update_conn();  // la hora se vuelve válida sin evento de MQTT: se revisa cada segundo
        command_publisher_tick();
        if (xTaskGetTickCount() - last_heap > pdMS_TO_TICKS(10000)) {
            last_heap = xTaskGetTickCount();
            log_heap();
        }
    }
}

esp_err_t net_task_start(const mg_settings_t *cfg)
{
    static char mqtt_uri[96];
    mg_settings_mqtt_uri(cfg, mqtt_uri, sizeof(mqtt_uri));
    s_net.rx = xQueueCreate(RX_QUEUE_LEN, sizeof(rx_msg_t));
    if (!s_net.rx) {
        return ESP_ERR_NO_MEM;
    }
    ESP_ERROR_CHECK(esp_event_handler_register(MG_WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(MG_MQTT_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));

    const mg_wifi_config_t wcfg = {
        .hostname = SYS_NODE_ID,
        .ssid = cfg->wifi_ssid,
        .password = cfg->wifi_pass,
    };
    esp_err_t err = mg_wifi_start(&wcfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi no iniciado: %s", esp_err_to_name(err));
        return err;
    }
    mg_time_start(NULL);

    mg_topic(s_net.topic_status, sizeof(s_net.topic_status), MG_NODE_DISP, SYS_NODE_ID, MG_CH_STATUS);
    mg_topic(s_net.topic_cmd, sizeof(s_net.topic_cmd), MG_NODE_DISP, SYS_NODE_ID, MG_CH_CMD);
    mg_topic(s_net.topic_ack, sizeof(s_net.topic_ack), MG_NODE_DISP, SYS_NODE_ID, MG_CH_CMD_ACK);
    s_net.lwt = build_status(MG_STATE_OFFLINE);
    const mg_mqtt_config_t mcfg = {
        .uri = mqtt_uri,
        .password = cfg->mqtt_pass,
        .client_id = SYS_NODE_ID,
        .username = CONFIG_MG_MQTT_USERNAME[0] ? NULL : SYS_NODE_ID,
        .lwt_topic = s_net.topic_status,
        .lwt_msg = s_net.lwt,
        .lwt_qos = 1,
        .lwt_retain = true,
    };
    ESP_ERROR_CHECK(mg_mqtt_start(&mcfg));
    const char *filters[] = { "growlab/+/+/status", "growlab/ctrl/+/state", "growlab/+/+/event",
                              "growlab/cam/+/cmd/ack" };
    for (size_t i = 0; i < sizeof(filters) / sizeof(filters[0]); i++) {
        mg_mqtt_subscribe(filters[i], 1, on_mqtt_msg, NULL);
    }
    mg_mqtt_subscribe(s_net.topic_cmd, 1, on_mqtt_msg, NULL);  // comandos propios (ota_update)

    BaseType_t ok = xTaskCreatePinnedToCore(net_task, "net_task", SYS_NET_TASK_STACK, NULL,
                                            SYS_NET_TASK_PRIO, NULL, SYS_CORE_NET);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
