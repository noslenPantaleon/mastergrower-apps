/**
 * @file upload_task.h
 * @brief Subida de capturas a la API (C5, Contrato §5.2; núcleo 0).
 *
 * Fuente principal: la cola persistente de la SD (storage_queue). Orden: prioridad alta y más
 * antigua primero. Cada captura se borra de la SD SOLO tras 201/200 de la API; con 4xx de datos
 * se descarta (reintentar no la arregla); sin red, 5xx, 401/403 se reintenta con backoff.
 * Sin tarjeta, las capturas se suben directo desde RAM con pocos reintentos (si fallan se pierden).
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "pipeline.h"

typedef struct {
    uint32_t uploaded;        /**< 201/200 desde el arranque */
    uint32_t discarded;    /**< rechazadas por la API (4xx de datos) o perdidas en RAM */
    uint32_t failures;         /**< intentos fallidos (red, 5xx, auth) */
    int last_status;       /**< último código HTTP (0 = sin respuesta) */
    uint32_t backoff_s;      /**< espera actual antes del próximo intento (0 = ninguna) */
} upload_stats_t;

/** @brief Crea la tarea. Requiere mg_wifi iniciado. */
esp_err_t upload_task_start(void);

/**
 * @brief Entrega un job que terminó el pipeline (toma posesión). Si está en la SD, solo despierta
 *        a la tarea y libera la RAM; si no, lo encola para subirlo desde memoria.
 */
void upload_task_submit(capture_job_t *job);

void upload_task_get_stats(upload_stats_t *out);
