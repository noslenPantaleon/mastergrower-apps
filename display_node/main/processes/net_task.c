/**
 * @file net_task.c
 * @brief Ver net_task.h.
 */
#include "net_task.h"

#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mg_contract.h"
#include "mg_mqtt.h"
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

static struct {
    QueueHandle_t rx;
    char topic_status[MG_TOPIC_MAX_LEN];
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

static void handle(rx_msg_t *r)
{
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
    }
    update_conn();
}

/* ---------------------------------------------------------------- tarea */

static void net_task(void *arg)
{
    TickType_t last_status = xTaskGetTickCount();
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
    }
}

esp_err_t net_task_start(void)
{
    s_net.rx = xQueueCreate(RX_QUEUE_LEN, sizeof(rx_msg_t));
    if (!s_net.rx) {
        return ESP_ERR_NO_MEM;
    }
    ESP_ERROR_CHECK(esp_event_handler_register(MG_WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(MG_MQTT_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));

    const mg_wifi_config_t wcfg = { .hostname = SYS_NODE_ID };
    esp_err_t err = mg_wifi_start(&wcfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi no iniciado: %s", esp_err_to_name(err));
        return err;
    }
    mg_time_start(NULL);

    mg_topic(s_net.topic_status, sizeof(s_net.topic_status), MG_NODE_DISP, SYS_NODE_ID, MG_CH_STATUS);
    s_net.lwt = build_status(MG_STATE_OFFLINE);
    const mg_mqtt_config_t mcfg = {
        .client_id = SYS_NODE_ID,
        .username = CONFIG_MG_MQTT_USERNAME[0] ? NULL : SYS_NODE_ID,
        .lwt_topic = s_net.topic_status,
        .lwt_msg = s_net.lwt,
        .lwt_qos = 1,
        .lwt_retain = true,
    };
    ESP_ERROR_CHECK(mg_mqtt_start(&mcfg));
    const char *filters[] = { "growlab/+/+/status", "growlab/ctrl/+/state", "growlab/+/+/event" };
    for (size_t i = 0; i < sizeof(filters) / sizeof(filters[0]); i++) {
        mg_mqtt_subscribe(filters[i], 1, on_mqtt_msg, NULL);
    }

    BaseType_t ok = xTaskCreatePinnedToCore(net_task, "net_task", SYS_NET_TASK_STACK, NULL,
                                            SYS_NET_TASK_PRIO, NULL, SYS_CORE_NET);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
