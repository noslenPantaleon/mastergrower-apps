/**
 * @file state_store.h
 * @brief Modelo en memoria del sistema (Arquitectura de pantalla §3.2): lo que publican los nodos
 *        por MQTT (status/state y eventos) y el estado de la conexión propia.
 *
 * Lo alimenta la red (net_task) y lo leen las pantallas mediante copias (snapshots): nadie
 * comparte punteros con el modelo. Cada cambio avisa a los observadores registrados, que deben
 * ser breves (p. ej. ui_bridge encola el aviso para la tarea de LVGL).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "mg_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SS_MAX_NODES   16
#define SS_MAX_EVENTS  32
#define SS_ID_LEN      16

typedef struct {
    char node_id[SS_ID_LEN];
    mg_node_kind_t kind;
    char node_state[16];     /**< tal cual llegó: operational, offline, ... */
    char camera_type[24];
    char firmware[16];
    char zone_id[25];
    time_t status_ts;        /**< ts del último status (0 = desconocido) */
    time_t last_capture;     /**< 0 = nunca / desconocido */
    int sd_free_pct;         /**< -1 = desconocido */
    int rssi;                /**< 0 = desconocido */
    int32_t config_version;  /**< -1 = no informado */
    int64_t rx_us;           /**< esp_timer al recibirlo */
} ss_node_t;

typedef struct {
    char node_id[SS_ID_LEN];
    char event_type[24];
    char severity[8];
    time_t ts;
    char detail[96];
} ss_event_t;

typedef struct {
    bool wifi;
    bool mqtt;
    bool time_valid;
} ss_conn_t;

typedef enum {
    SS_CHANGE_NODE,   /**< cambió un nodo (node_id) */
    SS_CHANGE_EVENT,  /**< llegó un evento (node_id del emisor) */
    SS_CHANGE_CONN,   /**< cambió WiFi, MQTT o la hora */
    SS_CHANGE_CMD,    /**< cambió el estado de un comando enviado a node_id (command_publisher) */
} ss_change_t;

typedef void (*ss_listener_t)(ss_change_t change, const char *node_id);

void state_store_init(void);

/** @brief Hasta 4 observadores; se llaman en la tarea que produjo el cambio. */
void state_store_subscribe(ss_listener_t cb);

/** @brief Avisa a los observadores un cambio que no es del modelo (p. ej. SS_CHANGE_CMD). */
void state_store_notify(ss_change_t change, const char *node_id);

/** @brief Aplica un status (cam/disp) o state (ctrl). Devuelve false si el JSON no sirve. */
bool state_store_apply_status(mg_node_kind_t kind, const char *node_id, const char *json, size_t len);

/** @brief Agrega un evento al historial (el más nuevo primero). */
bool state_store_apply_event(const char *node_id, const char *json, size_t len);

void state_store_set_conn(const ss_conn_t *c);
ss_conn_t state_store_conn(void);

/** @brief Copia los nodos ordenados por node_id. Devuelve cuántos. */
int state_store_nodes(ss_node_t *out, int max);
bool state_store_node(const char *node_id, ss_node_t *out);

/** @brief Copia los eventos del más nuevo al más viejo. */
int state_store_events(ss_event_t *out, int max);

/**
 * @brief Segundos desde el último dato del nodo: por su ts si hay hora válida (sirve también
 *        para mensajes retenidos viejos), si no desde que se recibió.
 */
int64_t state_store_age_s(const ss_node_t *n);

/** @brief Dato antiguo: el nodo no está offline pero hace más de CONFIG_MG_STALE_AFTER_S que no informa. */
bool state_store_is_stale(const ss_node_t *n);

#ifdef __cplusplus
}
#endif
