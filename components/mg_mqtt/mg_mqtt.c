/**
 * @file mg_mqtt.c
 * @brief Implementación de mg_mqtt sobre espressif/mqtt (esp-mqtt).
 */
#include "mg_mqtt.h"

#include <stdlib.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mqtt_client.h"
#include "sdkconfig.h"
#if CONFIG_MG_MQTT_USE_TLS
#include "esp_crt_bundle.h"
#endif

ESP_EVENT_DEFINE_BASE(MG_MQTT_EVENT);

static const char *TAG = "mg_mqtt";

#define MAX_TOPIC_LEN 128

typedef struct {
    char *filter;
    int qos;
    mg_mqtt_msg_cb_t cb;
    void *ctx;
} sub_t;

static struct {
    esp_mqtt_client_handle_t client;
    SemaphoreHandle_t lock;
    sub_t subs[CONFIG_MG_MQTT_MAX_SUBSCRIPTIONS];
    int n_subs;
    volatile bool connected;
    /* Reensamblado de mensajes fragmentados (solo lo usa la tarea del cliente MQTT). */
    char *rx_buf;
    char rx_topic[MAX_TOPIC_LEN];
    size_t rx_topic_len;
    size_t rx_total;
    bool rx_dropping;
} s_mq;

bool mg_mqtt_topic_matches(const char *filter, const char *topic, size_t topic_len)
{
    const char *f = filter;
    const char *t = topic;
    const char *t_end = topic + topic_len;

    while (*f) {
        if (*f == '#') {
            return f[1] == '\0';  // '#' debe ser el último nivel; abarca el resto
        }
        if (*f == '+') {
            while (t < t_end && *t != '/') {
                t++;  // consume un nivel completo
            }
            f++;
        } else {
            if (t >= t_end && strcmp(f, "/#") == 0) {
                return true;  // "a/#" también coincide con "a" (spec MQTT 4.7.1.2)
            }
            if (t >= t_end || *f != *t) {
                return false;
            }
            f++;
            t++;
            continue;
        }
        /* tras '+': ambos deben estar en un separador o terminar juntos */
        if (*f == '\0') {
            return t == t_end;
        }
        if (*f != '/' || t >= t_end || *t != '/') {
            return false;
        }
    }
    return t == t_end;
}

static void dispatch(const char *topic, size_t topic_len, const char *data, size_t data_len)
{
    const mg_mqtt_msg_t msg = { topic, topic_len, data, data_len };
    bool handled = false;

    xSemaphoreTake(s_mq.lock, portMAX_DELAY);
    for (int i = 0; i < s_mq.n_subs; i++) {
        if (mg_mqtt_topic_matches(s_mq.subs[i].filter, topic, topic_len)) {
            s_mq.subs[i].cb(&msg, s_mq.subs[i].ctx);
            handled = true;
        }
    }
    xSemaphoreGive(s_mq.lock);

    if (!handled) {
        ESP_LOGD(TAG, "Mensaje sin destinatario en %.*s", (int)topic_len, topic);
    }
}

static void on_data(esp_mqtt_event_handle_t ev)
{
    /* Mensaje completo en un solo evento: caso habitual. */
    if (ev->current_data_offset == 0 && ev->data_len == ev->total_data_len) {
        dispatch(ev->topic, ev->topic_len, ev->data, ev->data_len);
        return;
    }

    /* Mensaje fragmentado: el tema solo llega en el primer fragmento. */
    if (ev->current_data_offset == 0) {
        s_mq.rx_dropping = ev->total_data_len > CONFIG_MG_MQTT_MAX_RX_MSG || ev->topic_len >= MAX_TOPIC_LEN;
        if (s_mq.rx_dropping) {
            ESP_LOGW(TAG, "Mensaje de %d bytes descartado (máx %d)", ev->total_data_len,
                     CONFIG_MG_MQTT_MAX_RX_MSG);
            return;
        }
        memcpy(s_mq.rx_topic, ev->topic, ev->topic_len);
        s_mq.rx_topic_len = ev->topic_len;
        s_mq.rx_total = ev->total_data_len;
    }
    if (s_mq.rx_dropping) {
        return;
    }
    memcpy(s_mq.rx_buf + ev->current_data_offset, ev->data, ev->data_len);
    if (ev->current_data_offset + ev->data_len >= (int)s_mq.rx_total) {
        dispatch(s_mq.rx_topic, s_mq.rx_topic_len, s_mq.rx_buf, s_mq.rx_total);
    }
}

static void subscribe_all(void)
{
    xSemaphoreTake(s_mq.lock, portMAX_DELAY);
    for (int i = 0; i < s_mq.n_subs; i++) {
        int id = esp_mqtt_client_subscribe(s_mq.client, s_mq.subs[i].filter, s_mq.subs[i].qos);
        ESP_LOGI(TAG, "Suscripto a %s (qos %d, msg_id %d)", s_mq.subs[i].filter, s_mq.subs[i].qos, id);
    }
    xSemaphoreGive(s_mq.lock);
}

static void on_mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t ev = data;

    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Conectado al broker");
        s_mq.connected = true;
        subscribe_all();
        esp_event_post(MG_MQTT_EVENT, MG_MQTT_EVENT_CONNECTED, NULL, 0, 0);
        break;

    case MQTT_EVENT_DISCONNECTED:
        if (s_mq.connected) {
            ESP_LOGW(TAG, "Desconectado del broker (reintenta solo)");
            s_mq.connected = false;
            esp_event_post(MG_MQTT_EVENT, MG_MQTT_EVENT_DISCONNECTED, NULL, 0, 0);
        }
        break;

    case MQTT_EVENT_DATA:
        on_data(ev);
        break;

    case MQTT_EVENT_ERROR:
        if (ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGE(TAG, "Broker rechazó la conexión (código %d): revisar usuario/clave/ACL",
                     ev->error_handle->connect_return_code);
        } else if (ev->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGW(TAG, "Error de transporte (errno %d)", ev->error_handle->esp_transport_sock_errno);
        }
        break;

    default:
        break;
    }
}

esp_err_t mg_mqtt_start(const mg_mqtt_config_t *cfg)
{
    if (s_mq.client) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_mq.lock) {
        s_mq.lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_mq.lock, ESP_ERR_NO_MEM, TAG, "mutex");
    }
    if (!s_mq.rx_buf) {
        s_mq.rx_buf = malloc(CONFIG_MG_MQTT_MAX_RX_MSG);
        ESP_RETURN_ON_FALSE(s_mq.rx_buf, ESP_ERR_NO_MEM, TAG, "rx_buf");
    }

    const mg_mqtt_config_t empty = { 0 };
    if (!cfg) {
        cfg = &empty;
    }

    esp_mqtt_client_config_t mc = {
        .broker.address.uri = cfg->uri ? cfg->uri : CONFIG_MG_MQTT_BROKER_URI,
        .credentials.client_id = cfg->client_id,
        .credentials.username = cfg->username ? cfg->username : CONFIG_MG_MQTT_USERNAME,
        .credentials.authentication.password = cfg->password ? cfg->password : CONFIG_MG_MQTT_PASSWORD,
        .session.keepalive = CONFIG_MG_MQTT_KEEPALIVE_S,
        .network.reconnect_timeout_ms = 5000,
    };
#if CONFIG_MG_MQTT_USE_TLS
    mc.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
#endif
    if (cfg->lwt_topic && cfg->lwt_msg) {
        mc.session.last_will.topic = cfg->lwt_topic;
        mc.session.last_will.msg = cfg->lwt_msg;
        mc.session.last_will.msg_len = (int)strlen(cfg->lwt_msg);
        mc.session.last_will.qos = cfg->lwt_qos;
        mc.session.last_will.retain = cfg->lwt_retain;
    }

    s_mq.client = esp_mqtt_client_init(&mc);
    ESP_RETURN_ON_FALSE(s_mq.client, ESP_FAIL, TAG, "esp_mqtt_client_init");

    esp_err_t err = esp_mqtt_client_register_event(s_mq.client, ESP_EVENT_ANY_ID, on_mqtt_event, NULL);
    if (err == ESP_OK) {
        err = esp_mqtt_client_start(s_mq.client);
    }
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(s_mq.client);
        s_mq.client = NULL;
        return err;
    }
    ESP_LOGI(TAG, "Conectando a %s como \"%s\"%s", mc.broker.address.uri,
             mc.credentials.username ? mc.credentials.username : "",
             cfg->lwt_topic ? " (con LWT)" : "");
    return ESP_OK;
}

esp_err_t mg_mqtt_stop(void)
{
    if (!s_mq.client) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_mqtt_client_stop(s_mq.client);
    esp_mqtt_client_destroy(s_mq.client);
    s_mq.client = NULL;
    s_mq.connected = false;
    return ESP_OK;
}

bool mg_mqtt_is_connected(void)
{
    return s_mq.connected;
}

int mg_mqtt_publish(const char *topic, const char *payload, int len, int qos, bool retain)
{
    if (!s_mq.client || !topic) {
        return -1;
    }
    if (len < 0) {
        len = payload ? (int)strlen(payload) : 0;
    }
    /* enqueue: no bloquea esperando la red; QoS>0 se reintenta desde la outbox del cliente. */
    int id = esp_mqtt_client_enqueue(s_mq.client, topic, payload, len, qos, retain, true);
    if (id < 0) {
        ESP_LOGW(TAG, "No se pudo publicar en %s", topic);
    }
    return id;
}

esp_err_t mg_mqtt_subscribe(const char *filter, int qos, mg_mqtt_msg_cb_t cb, void *ctx)
{
    ESP_RETURN_ON_FALSE(filter && cb && qos >= 0 && qos <= 2, ESP_ERR_INVALID_ARG, TAG, "args");
    if (!s_mq.lock) {
        s_mq.lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_mq.lock, ESP_ERR_NO_MEM, TAG, "mutex");
    }

    xSemaphoreTake(s_mq.lock, portMAX_DELAY);
    if (s_mq.n_subs >= CONFIG_MG_MQTT_MAX_SUBSCRIPTIONS) {
        xSemaphoreGive(s_mq.lock);
        return ESP_ERR_NO_MEM;
    }
    char *copy = strdup(filter);
    if (!copy) {
        xSemaphoreGive(s_mq.lock);
        return ESP_ERR_NO_MEM;
    }
    s_mq.subs[s_mq.n_subs++] = (sub_t){ .filter = copy, .qos = qos, .cb = cb, .ctx = ctx };
    xSemaphoreGive(s_mq.lock);

    if (s_mq.connected) {
        esp_mqtt_client_subscribe(s_mq.client, copy, qos);
    }
    return ESP_OK;
}
