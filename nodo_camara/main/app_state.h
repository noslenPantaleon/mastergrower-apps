/**
 * @file app_state.h
 * @brief Estado observable del nodo (lo que se publica en growlab/cam/{id}/status).
 *        Seguro para usar desde cualquier tarea. Cada cambio relevante pide republicar el status.
 */
#pragma once

#include <time.h>
#include "mg_contract.h"

void app_state_init(void);

void app_state_set_estado(mg_estado_nodo_t estado);
mg_estado_nodo_t app_state_get_estado(void);

/** @brief Registra una captura exitosa (t = hora UTC de la captura). */
void app_state_set_ultima_captura(time_t t);
/** @return hora de la última captura o 0 si no hubo. */
time_t app_state_get_ultima_captura(void);
