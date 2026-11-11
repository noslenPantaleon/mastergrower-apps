/**
 * @file mg_mqtt.h
 * @brief Cliente MQTT del sistema: conexión con LWT, publicación y suscripciones con callback
 *        por filtro de tema (+ y #). Las suscripciones se rehacen solas al reconectar.
 *
 * Los callbacks corren en la tarea interna del cliente MQTT: deben ser CORTOS
 * (copiar el mensaje a una cola y volver). No llamar mg_mqtt_stop() desde un callback.
 *
 * Eventos en el event loop por defecto (base MG_MQTT_EVENT):
 *   MG_MQTT_EVENT_CONNECTED, MG_MQTT_EVENT_DISCONNECTED (sin datos)
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(MG_MQTT_EVENT);

typedef enum {
    MG_MQTT_EVENT_CONNECTED,
    MG_MQTT_EVENT_DISCONNECTED,
} mg_mqtt_event_id_t;

/** Mensaje recibido. topic y data NO terminan en '\0'; usar los largos. Válidos solo durante el callback. */
typedef struct {
    const char *topic;
    size_t topic_len;
    const char *data;
    size_t data_len;
} mg_mqtt_msg_t;

typedef void (*mg_mqtt_msg_cb_t)(const mg_mqtt_msg_t *msg, void *ctx);

typedef struct {
    const char *uri;        /**< NULL → CONFIG_MG_MQTT_BROKER_URI */
    const char *client_id;  /**< recomendado: node_id */
    const char *username;   /**< NULL → CONFIG_MG_MQTT_USERNAME */
    const char *password;   /**< NULL → CONFIG_MG_MQTT_PASSWORD */
    /* Last Will (opcional): lo publica el broker si el nodo se cae sin desconectarse. */
    const char *lwt_topic;
    const char *lwt_msg;
    int lwt_qos;
    bool lwt_retain;
} mg_mqtt_config_t;

esp_err_t mg_mqtt_start(const mg_mqtt_config_t *cfg);
esp_err_t mg_mqtt_stop(void);
bool mg_mqtt_is_connected(void);

/**
 * @brief Publica un mensaje (no bloquea: QoS>0 queda en la cola del cliente).
 * @param len  -1 → strlen(payload)
 * @return msg_id (>=0) o -1 si falló (p. ej. sin conexión con QoS 0).
 */
int mg_mqtt_publish(const char *topic, const char *payload, int len, int qos, bool retain);

/**
 * @brief Registra una suscripción. Si ya hay conexión se suscribe en el momento; si no,
 *        al conectar. El filtro se copia.
 */
esp_err_t mg_mqtt_subscribe(const char *filter, int qos, mg_mqtt_msg_cb_t cb, void *ctx);

/** @brief true si topic (con largo) coincide con el filtro MQTT (+ y #). Expuesto para tests. */
bool mg_mqtt_topic_matches(const char *filter, const char *topic, size_t topic_len);

#ifdef __cplusplus
}
#endif
