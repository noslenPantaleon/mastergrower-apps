/**
 * @file capture_task.h
 * @brief Etapa de captura (núcleo 1, prioridad alta): consume q_trigger, ajusta la resolución,
 *        captura, copia el JPEG a un job en PSRAM, libera el framebuffer y lo pasa a q_frame.
 *        Si el disparo vino de un comando, responde el ack "done" (o "rejected" si falla).
 */
#pragma once

#include "esp_err.h"

esp_err_t capture_task_start(void);
