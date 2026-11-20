/**
 * @file mg_contract.h
 * @brief Contrato de interfaces en C (fuente: contract/ del repositorio, schema_version 1).
 *        Todos los módulos ESP32 arman y leen mensajes con estas funciones para no
 *        duplicar nombres de temas, campos ni enumeraciones.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MG_SCHEMA_VERSION 1
#define MG_TOPIC_MAX_LEN  64
#define MG_CMD_ID_MAX_LEN 64
/** Ventana de vigencia de un comando (Contrato §9): más viejo que esto se rechaza. */
#define MG_CMD_VALIDITY_S 60

/* ---------- Temas: growlab/{tipo}/{node_id}/{canal} ---------- */

typedef enum { MG_NODE_CAM, MG_NODE_CTRL, MG_NODE_DISP } mg_node_kind_t;

#define MG_CH_STATUS  "status"
#define MG_CH_STATE   "state"
#define MG_CH_EVENT   "event"
#define MG_CH_CMD     "cmd"
#define MG_CH_CMD_ACK "cmd/ack"

/** @brief Arma el tema. node_id puede ser "+" para filtros. Devuelve ESP_ERR_INVALID_SIZE si no entra. */
esp_err_t mg_topic(char *buf, size_t len, mg_node_kind_t kind, const char *node_id, const char *channel);

/* ---------- Enumeraciones (Contrato §3.3) ---------- */

typedef enum {
    MG_STATE_BOOTING, MG_STATE_OPERATIONAL, MG_STATE_AUTONOMOUS, MG_STATE_ERROR, MG_STATE_OFFLINE,
} mg_node_state_t;

typedef enum { MG_SEV_LOW, MG_SEV_MEDIUM, MG_SEV_HIGH } mg_severity_t;

typedef enum {
    MG_EVT_EARLY_LIGHTS_OFF, MG_EVT_PLANT_ANOMALY, MG_EVT_SD_FULL, MG_EVT_CAMERA_ERROR,
    MG_EVT_NODE_DISCONNECTED,
} mg_event_type_t;

typedef enum {
    MG_TRIGGER_TIMER, MG_TRIGGER_SENSOR_EVENT, MG_TRIGGER_REMOTE_COMMAND, MG_TRIGGER_POST_ACTUATION,
    MG_TRIGGER_SCHEDULE,
} mg_trigger_type_t;

typedef enum {
    MG_ACTION_UNKNOWN = 0,
    MG_ACTION_CAPTURE_NOW, MG_ACTION_REFRESH_CONFIG, MG_ACTION_REBOOT, MG_ACTION_START_STREAM,
    MG_ACTION_STOP_STREAM, MG_ACTION_SET_ACTUATOR, MG_ACTION_SET_MODE,
} mg_action_t;

typedef enum { MG_ACK_ACCEPTED, MG_ACK_REJECTED, MG_ACK_DONE } mg_ack_result_t;

const char *mg_node_state_str(mg_node_state_t v);
const char *mg_severity_str(mg_severity_t v);
const char *mg_event_type_str(mg_event_type_t v);
const char *mg_trigger_type_str(mg_trigger_type_t v);
const char *mg_action_str(mg_action_t v);
const char *mg_ack_result_str(mg_ack_result_t v);
mg_action_t mg_action_from_str(const char *s);

/* ---------- Mensajes ---------- */

/**
 * @brief Objeto JSON base con schema_version, node_id y ts (hora actual UTC).
 *        Liberar con cJSON_Delete o serializar con mg_msg_to_string.
 */
cJSON *mg_msg_new(const char *node_id);

/** @brief Serializa y LIBERA el objeto. Devuelve string con malloc (liberar con free) o NULL. */
char *mg_msg_to_string(cJSON *msg);

/** Campos de growlab/cam/{id}/status. Los opcionales en NULL / <0 se publican como null. */
typedef struct {
    mg_node_state_t node_state;
    const char *camera_type;
    const char *firmware;
    const char *last_capture;  /**< ISO-8601 o NULL */
    int sd_free_pct;            /**< -1 = desconocido (null) */
    int rssi;                    /**< 0 = desconocido (null) */
    const char *zone_id;         /**< NULL = sin asignar */
    bool has_config_version;     /**< false = no se publica config_version (p. ej. en el LWT) */
    uint32_t config_version;     /**< versión de la config aplicada (0 = valores por defecto) */
} mg_cam_status_t;

char *mg_msg_cam_status(const char *node_id, const mg_cam_status_t *st);

/** growlab/cam/{id}/event */
char *mg_msg_cam_event(const char *node_id, mg_event_type_t type, mg_severity_t sev,
                       const char *capture_id, const char *detail);

/** growlab/{tipo}/{id}/cmd/ack */
char *mg_msg_cmd_ack(const char *node_id, const char *cmd_id, mg_ack_result_t res, const char *reason);

/** Comando recibido (growlab/{tipo}/{id}/cmd). */
typedef struct {
    mg_action_t action;
    char action_raw[24];               /**< texto original (para rechazar desconocidas) */
    char cmd_id[MG_CMD_ID_MAX_LEN + 1];
    char sender[32];                   /**< node_id del emisor */
    time_t ts;                         /**< 0 si no vino o no se pudo interpretar */
    cJSON *params;                 /**< propiedad del mg_cmd_t; NULL si no hay */
} mg_cmd_t;

/**
 * @brief Interpreta y valida un comando. Exige schema_version 1, cmd_id y action.
 * @return ESP_OK; ESP_ERR_INVALID_ARG si el JSON o los campos obligatorios son inválidos;
 *         ESP_ERR_NOT_SUPPORTED si schema_version no es compatible.
 *         Si devuelve error, out->cmd_id igual se completa si se pudo leer (para el ack).
 */
esp_err_t mg_cmd_parse(const char *json, size_t len, mg_cmd_t *out);
void mg_cmd_free(mg_cmd_t *cmd);

#ifdef __cplusplus
}
#endif
