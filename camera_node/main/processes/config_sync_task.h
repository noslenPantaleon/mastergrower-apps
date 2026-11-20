/**
 * @file config_sync_task.h
 * @brief Sincroniza la configuración remota (C6, Contrato §7).
 *
 * Consulta GET /config/nodes/{id} con If-None-Match (versión vigente) al arrancar, al recibir
 * refresh_config y cada CONFIG_MG_CONFIG_SYNC_PERIOD_S. Si la versión cambió la guarda en NVS
 * (config_store) y la aplica al pipeline. Sin red sigue con la última guardada.
 */
#pragma once

#include "config_store.h"
#include "esp_err.h"
#include "trigger_manager.h"

/** @brief Traduce la config del nodo a la del trigger_manager (resoluciones, ventana de luz). */
void config_sync_to_trigger(const node_config_t *c, trigger_config_t *out);

/** @brief Aplica la config vigente al pipeline (trigger_manager y calidad JPEG). */
void config_sync_apply(void);

/** @brief Arranca la tarea (núcleo 0) y registra el handler de refresh_config. */
esp_err_t config_sync_task_start(void);
