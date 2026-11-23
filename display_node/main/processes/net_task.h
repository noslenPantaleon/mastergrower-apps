/**
 * @file net_task.h
 * @brief Red del nodo de pantalla (núcleo 0): WiFi, hora (NTP) y MQTT.
 *
 * Se suscribe a status/state, event y growlab/cam/+/cmd/ack; el callback de
 * MQTT solo copia el mensaje a una cola y esta tarea lo interpreta y actualiza el state_store.
 * Publica su propio status retenido (LWT offline) en growlab/disp/{id}/status.
 */
#pragma once

#include "esp_err.h"
#include "mg_contract.h"

esp_err_t net_task_start(void);

/** @brief Publica un acuse en growlab/disp/{id}/cmd/ack (seguro desde cualquier tarea). */
void net_task_send_ack(const char *cmd_id, mg_ack_result_t res, const char *reason);

/** @brief Pide republicar el status propio (p. ej. tras confirmar un firmware nuevo). */
void net_task_request_status(void);
