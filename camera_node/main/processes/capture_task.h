/**
 * @file capture_task.h
 * @brief Etapa de captura (núcleo 1, prioridad alta): consume q_trigger, ajusta la resolución,
 *        captura, copia el JPEG a un job en PSRAM, libera el framebuffer y lo pasa a q_frame.
 *        Si el disparo vino de un comando, responde el ack "done" (o "rejected" si falla).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t capture_task_start(void);

/**
 * @brief Calidad JPEG a usar desde la próxima captura (config remota). La aplica la propia
 *        capture_task antes de capturar, así el sensor nunca se toca desde otra tarea.
 */
void capture_task_set_quality(uint8_t jpeg_quality);

/**
 * @brief Candado de la cámara para secuencias "cambiar resolución + capturar" (C8): lo toman
 *        capture_task y stream_task, así un capture_now durante el stream sale en resolución
 *        completa y el stream después vuelve a la suya.
 * @return true si se obtuvo antes de `timeout`.
 */
bool capture_task_camera_lock(TickType_t timeout);
void capture_task_camera_unlock(void);
