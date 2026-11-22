/**
 * @file stream_task.h
 * @brief Vista en vivo bajo demanda (C8, Arquitectura §3.1.7, Contrato §5.6, SRS F-09).
 *
 * start_stream → abre un WebSocket de publicación hacia la API (token del nodo) y envía cuadros
 * JPEG en QVGA/VGA a fps_max como máximo, hasta stop_stream, ttl_s, que la API cierre la sesión o
 * un error. Mientras dura, el timer de capturas se difiere; un capture_now toma la cámara entre
 * dos cuadros, captura en resolución completa y el stream retoma.
 *
 * Acks: start_stream → "accepted" y, cuando salió el primer cuadro, "done" (o "rejected" con
 * stream_connect_failed / camera_error). stop_stream → "done".
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

/** @brief Crea la tarea y registra los handlers de start_stream y stop_stream en net_task. */
esp_err_t stream_task_start(void);

/** @brief true si hay una sesión transmitiendo (para el status y la depuración). */
bool stream_task_active(void);
