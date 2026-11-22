/**
 * @file screens.h
 * @brief Pantallas de la variante C. Cada una se crea una vez dentro del área de contenido
 *        (800 × 360) y se refresca solo mientras está visible.
 */
#pragma once

#include <stdint.h>
#include "lvgl.h"

#define SCR_W  800
#define SCR_H  (480 - 48 - 72)

void scr_home_create(lv_obj_t *parent);
void scr_home_refresh(uint32_t dirty);

void scr_cameras_create(lv_obj_t *parent);
void scr_cameras_refresh(uint32_t dirty);

void scr_events_create(lv_obj_t *parent);
void scr_events_refresh(uint32_t dirty);

/** @brief Vista en vivo: se crea una vez; enter/leave abren y cierran la sesión. */
void scr_live_create(lv_obj_t *parent, void (*on_back)(void));
void scr_live_enter(const char *node_id);
void scr_live_leave(void);

/** @brief Abre la vista en vivo de una cámara (lo implementa ui_shell). */
void ui_shell_open_live(const char *node_id);

/** @brief Pantalla pendiente: título y explicación de qué falta. */
void scr_placeholder_create(lv_obj_t *parent, const char *title, const char *why);
