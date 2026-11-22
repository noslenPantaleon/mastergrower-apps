/**
 * @file ui_bridge.h
 * @brief Puente modelo → interfaz (Arquitectura de pantalla §3.3): los hilos de red nunca tocan
 *        LVGL; los cambios del state_store se encolan y un timer de LVGL (que corre dentro de la
 *        tarea de LVGL) los aplica a la pantalla activa.
 */
#pragma once

#include "esp_err.h"

/** @brief Registra el observador del state_store y el timer de LVGL. Llamar con el candado de LVGL. */
esp_err_t ui_bridge_start(void);
