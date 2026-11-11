/**
 * @file net_task.c
 * @brief Ver net_task.h. Los callbacks de MQTT y del event loop solo encolan; todo el trabajo
 *        (armar JSON, validar, ejecutar handlers) ocurre en esta tarea.
 */
#include "net_task.h"

#include <stdlib.h>
#include <string.h>
#include "app_state.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hal_camera.h"
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
    net_cmd_handler_t handlers[MG_ACCION_SET_MODO + 1];
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
    post(NET_MSG_STATUS, NULL, 0);  // republicar con ts y ultima_captura correctos
}

/* ---------------------------------------------------------------- publicación */

static void publish_status(void)
{
    if (!mg_mqtt_is_connected()) {
        return;
    }
    char ultima[MG_TIME_ISO_LEN];
    time_t t = app_state_get_ultima_captura();
    if (t) {
        mg_time_to_iso(t, ultima, sizeof(ultima));
    }
    const mg_cam_status_t st = {
        .estado_nodo = app_state_get_estado(),
        .camera_type = hal_camera_type(),
        .firmware = SYS_FIRMWARE_VERSION,
        .ultima_captura = t ? ultima : NULL,
        .sd_libre_pct = -1,  // C4: tarjeta SD
        .rssi = mg_wifi_get_rssi(),
        .zona_id = NULL,     // C6: config remota
    };
    char *json = mg_msg_cam_status(SYS_NODE_ID, &st);
    if (json) {
        mg_mqtt_publish(s_net.topic_status, json, -1, 1, true);
        ESP_LOGI(TAG, "status → %s", json);
        free(json);
    }
}

esp_err_t net_task_send_ack(const char *cmd_id, mg_ack_result_t res, const char *motivo)
{
    char *json = mg_msg_cmd_ack(SYS_NODE_ID, cmd_id, res, motivo);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    int id = mg_mqtt_publish(s_net.topic_ack, json, -1, 1, false);
    ESP_LOGI(TAG, "ack → %s", json);
    free(json);
    return id >= 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t net_task_publish_event(mg_tipo_evento_t tipo, mg_severidad_t sev, const char *captura_id,
                                 const char *detalle)
{
    char *json = mg_msg_cam_event(SYS_NODE_ID, tipo, sev, captura_id, detalle);
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
                              err == ESP_ERR_NOT_SUPPORTED ? "schema_version_no_soportada" : "formato_invalido");
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

    ESP_LOGI(TAG, "cmd %s (cmd_id=%s, de %s)", cmd.accion_raw, cmd.cmd_id, cmd.emisor[0] ? cmd.emisor : "?");

    /* Vigencia: solo se puede verificar si ambos relojes son válidos. */
    if (cmd.ts && mg_time_is_valid()) {
        double age = difftime(time(NULL), cmd.ts);
        if (age > MG_CMD_VALIDITY_S || age < -MG_CMD_VALIDITY_S) {
            net_task_send_ack(cmd.cmd_id, MG_ACK_REJECTED, "vencido");
            mg_cmd_free(&cmd);
            return;
        }
    } else {
        ESP_LOGW(TAG, "No se puede verificar la vigencia de %s (sin ts o sin hora)", cmd.cmd_id);
    }

    net_cmd_handler_t h = s_net.handlers[cmd.accion];
    if (cmd.accion == MG_ACCION_DESCONOCIDA || !h) {
        net_task_send_ack(cmd.cmd_id, MG_ACK_REJECTED, "accion_no_soportada");
        mg_cmd_free(&cmd);
        return;
    }

    net_task_send_ack(cmd.cmd_id, MG_ACK_ACCEPTED, NULL);
    char motivo[64] = "";
    err = h(&cmd, motivo, sizeof(motivo));
    if (err == ESP_OK) {
        net_task_send_ack(cmd.cmd_id, MG_ACK_DONE, motivo[0] ? motivo : NULL);
    } else if (err != ESP_ERR_NOT_FINISHED) {
        net_task_send_ack(cmd.cmd_id, MG_ACK_REJECTED, motivo[0] ? motivo : esp_err_to_name(err));
    }
    mg_cmd_free(&cmd);
}

esp_err_t net_task_register_cmd(mg_accion_t accion, net_cmd_handler_t handler)
{
    if (accion == MG_ACCION_DESCONOCIDA || accion > MG_ACCION_SET_MODO || !handler) {
        return ESP_ERR_INVALID_ARG;
    }
    s_net.handlers[accion] = handler;
    return ESP_OK;
}

/* ---------------------------------------------------------------- tarea */

static void start_mqtt(void)
{
    /* LWT: lo publica el broker si el nodo desaparece sin desconectarse. */
    mg_cam_status_t lwt = {
        .estado_nodo = MG_ESTADO_FUERA_DE_LINEA,
        .camera_type = hal_camera_type(),
        .firmware = SYS_FIRMWARE_VERSION,
        .sd_libre_pct = -1,
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
            if (app_state_get_estado() != MG_ESTADO_ERROR) {  // un error de hardware no se borra por reconectar
                app_state_set_estado(MG_ESTADO_OPERATIVO);
            }
            publish_status();  // siempre al conectar: pisa el LWT retenido
            break;
        case NET_MSG_DISCONNECTED:
            if (app_state_get_estado() != MG_ESTADO_ERROR) {
                app_state_set_estado(MG_ESTADO_AUTONOMO);
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
