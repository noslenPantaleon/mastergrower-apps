/**
 * @file analysis_task.h
 * @brief Etapa de análisis local (núcleo 1, prioridad media). En C3 solo deja pasar los jobs;
 *        en C7 corre la heurística HSV y, según analysis_mode, el modelo reducido.
 */
#pragma once

#include "esp_err.h"

esp_err_t analysis_task_start(void);
