/**
 * @file net_task.h
 * @brief Red del nodo de pantalla (núcleo 0): WiFi, hora (NTP) y MQTT.
 *
 * Se suscribe a growlab/+/+/status, growlab/ctrl/+/state y growlab/+/+/event; el callback de
 * MQTT solo copia el mensaje a una cola y esta tarea lo interpreta y actualiza el state_store.
 * Publica su propio status retenido (LWT offline) en growlab/disp/{id}/status.
 */
#pragma once

#include "esp_err.h"

esp_err_t net_task_start(void);
