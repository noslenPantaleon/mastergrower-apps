/**
 * @file net_task.h
 * @brief Tarea de red MQTT del nodo de cámara (Arquitectura §3.2, núcleo 0):
 *        - publica growlab/cam/{id}/status retenido al conectar, al cambiar y cada
 *          CONFIG_MG_STATUS_PERIOD_S; LWT con node_state=offline
 *        - recibe growlab/cam/{id}/cmd, valida (schema, vigencia, cmd_id repetido),
 *          responde cmd/ack y despacha al handler registrado para la acción
 *        - publica eventos (growlab/cam/{id}/event)
 */
#pragma once

#include "esp_err.h"
#include "mg_contract.h"

/**
 * Handler de un comando. Corre en la net_task: debe ser breve o delegar en otra tarea.
 * Retorno:
 *   ESP_OK               → se responde "done"
 *   ESP_ERR_NOT_FINISHED → solo "accepted"; otra tarea enviará luego net_task_send_ack(done)
 *   otro                 → "rejected" con el motivo escrito en `motivo`
 */
typedef esp_err_t (*net_cmd_handler_t)(const mg_cmd_t *cmd, char *reason, size_t reason_len);

/** @brief Registrar antes de net_task_start(). */
esp_err_t net_task_register_cmd(mg_action_t action, net_cmd_handler_t handler);

/** @brief Crea la tarea. Requiere mg_wifi iniciado. Inicia SNTP y MQTT por su cuenta. */
/** @brief Broker y contraseña MQTT (de mg_settings). Llamar antes de net_task_start(). */
void net_task_set_broker(const char *uri, const char *password);

esp_err_t net_task_start(void);

/** @brief Pide republicar el status (asíncrono, seguro desde cualquier tarea). */
void net_task_request_status(void);

esp_err_t net_task_send_ack(const char *cmd_id, mg_ack_result_t res, const char *reason);

esp_err_t net_task_publish_event(mg_event_type_t type, mg_severity_t sev, const char *capture_id,
                                 const char *detail);
