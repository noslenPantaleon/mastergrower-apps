/**
 * @file net_setup.h
 * @brief Trabajos de la pantalla "Red y servidor" que no pueden correr en la tarea de LVGL:
 *        buscar redes (bloquea unos segundos) y guardar/borrar en NVS (escribe la flash, así
 *        que necesita una pila en RAM interna; la de LVGL está en PSRAM). Guardar reinicia.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "mg_settings.h"
#include "mg_wifi.h"

#define NET_SETUP_MAX_APS 16

/** @brief `current` es la configuración en uso (se copia: la pantalla nunca lee NVS). */
esp_err_t net_setup_start(const mg_settings_t *current);

/** @brief Pide una búsqueda de redes; el resultado se consulta con net_setup_scan_result(). */
void net_setup_scan(void);

/** @brief -1 = buscando, -2 = sin pedido, ≥ 0 = cantidad de redes (copiadas a out). Lo consume. */
int net_setup_scan_result(mg_wifi_ap_t *out, int max);

/** @brief Guarda en NVS y reinicia la pantalla. */
void net_setup_save_and_restart(const mg_settings_t *s);

/** @brief Borra lo guardado (vuelve a Kconfig) y reinicia. */
void net_setup_reset_and_restart(void);

/** @brief Configuración en uso (la que cargó app_main al arrancar). */
void net_setup_current(mg_settings_t *out);
