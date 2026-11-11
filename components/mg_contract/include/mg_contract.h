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
    MG_ESTADO_ARRANCANDO, MG_ESTADO_OPERATIVO, MG_ESTADO_AUTONOMO, MG_ESTADO_ERROR, MG_ESTADO_FUERA_DE_LINEA,
} mg_estado_nodo_t;

typedef enum { MG_SEV_BAJA, MG_SEV_MEDIA, MG_SEV_ALTA } mg_severidad_t;

typedef enum {
    MG_EVT_LUZ_APAGADA_ANTICIPADA, MG_EVT_ANOMALIA_PLANTA, MG_EVT_SD_LLENA, MG_EVT_ERROR_CAMARA,
    MG_EVT_NODO_SIN_CONEXION,
} mg_tipo_evento_t;

typedef enum {
    MG_TRIGGER_TIMER, MG_TRIGGER_EVENTO_SENSOR, MG_TRIGGER_COMANDO_REMOTO, MG_TRIGGER_POST_ACTUACION,
    MG_TRIGGER_CALENDARIO,
} mg_trigger_type_t;

typedef enum {
    MG_ACCION_DESCONOCIDA = 0,
    MG_ACCION_CAPTURE_NOW, MG_ACCION_REFRESH_CONFIG, MG_ACCION_REBOOT, MG_ACCION_START_STREAM,
    MG_ACCION_STOP_STREAM, MG_ACCION_SET_ACTUADOR, MG_ACCION_SET_MODO,
} mg_accion_t;

typedef enum { MG_ACK_ACCEPTED, MG_ACK_REJECTED, MG_ACK_DONE } mg_ack_result_t;

const char *mg_estado_nodo_str(mg_estado_nodo_t v);
const char *mg_severidad_str(mg_severidad_t v);
const char *mg_tipo_evento_str(mg_tipo_evento_t v);
const char *mg_trigger_type_str(mg_trigger_type_t v);
const char *mg_accion_str(mg_accion_t v);
const char *mg_ack_result_str(mg_ack_result_t v);
mg_accion_t mg_accion_from_str(const char *s);

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
    mg_estado_nodo_t estado_nodo;
    const char *camera_type;
    const char *firmware;
    const char *ultima_captura;  /**< ISO-8601 o NULL */
    int sd_libre_pct;            /**< -1 = desconocido (null) */
    int rssi;                    /**< 0 = desconocido (null) */
    const char *zona_id;         /**< NULL = sin asignar */
} mg_cam_status_t;

char *mg_msg_cam_status(const char *node_id, const mg_cam_status_t *st);

/** growlab/cam/{id}/event */
char *mg_msg_cam_event(const char *node_id, mg_tipo_evento_t tipo, mg_severidad_t sev,
                       const char *captura_id, const char *detalle);

/** growlab/{tipo}/{id}/cmd/ack */
char *mg_msg_cmd_ack(const char *node_id, const char *cmd_id, mg_ack_result_t res, const char *motivo);

/** Comando recibido (growlab/{tipo}/{id}/cmd). */
typedef struct {
    mg_accion_t accion;
    char accion_raw[24];               /**< texto original (para rechazar desconocidas) */
    char cmd_id[MG_CMD_ID_MAX_LEN + 1];
    char emisor[32];                   /**< node_id del emisor */
    time_t ts;                         /**< 0 si no vino o no se pudo interpretar */
    cJSON *parametros;                 /**< propiedad del mg_cmd_t; NULL si no hay */
} mg_cmd_t;

/**
 * @brief Interpreta y valida un comando. Exige schema_version 1, cmd_id y accion.
 * @return ESP_OK; ESP_ERR_INVALID_ARG si el JSON o los campos obligatorios son inválidos;
 *         ESP_ERR_NOT_SUPPORTED si schema_version no es compatible.
 *         Si devuelve error, out->cmd_id igual se completa si se pudo leer (para el ack).
 */
esp_err_t mg_cmd_parse(const char *json, size_t len, mg_cmd_t *out);
void mg_cmd_free(mg_cmd_t *cmd);

#ifdef __cplusplus
}
#endif
