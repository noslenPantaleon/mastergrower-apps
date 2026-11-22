/**
 * @file ui_api.h
 * @brief Única interfaz que la aplicación ve de la interfaz gráfica (Arquitectura de pantalla
 *        §3.6): la variante se elige al compilar (hoy solo la C, en ui/c/; D6 agrega LVGL Pro).
 *        Todas las funciones se llaman con el candado de LVGL tomado.
 */
#pragma once

#include <stdint.h>

/** Qué cambió desde el último refresco (se combinan con |). */
#define UI_DIRTY_NODES   (1u << 0)
#define UI_DIRTY_EVENTS  (1u << 1)
#define UI_DIRTY_CONN    (1u << 2)
#define UI_DIRTY_CMD     (1u << 3)
#define UI_DIRTY_TICK    (1u << 4)  /**< pasó 1 s: reloj y "hace N min" */
#define UI_DIRTY_THUMB   (1u << 5)  /**< hay miniaturas nuevas */
#define UI_DIRTY_ALL     0xffffffffu

/** @brief Tema, fuentes, barras y pantalla de inicio. */
void ui_init(void);

/** @brief Aplica los cambios a la barra superior y a la pantalla visible. */
void ui_refresh(uint32_t dirty);
