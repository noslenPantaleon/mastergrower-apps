/**
 * @file ui_status.h
 * @brief Pantalla de estado de D1: conexión, nodos (con marca de dato antiguo) y últimos eventos.
 *        Todas las funciones se llaman con el candado de LVGL (desde la tarea de LVGL).
 *        En D2 la reemplazan Inicio y Cámaras.
 */
#pragma once

void ui_status_create(void);
void ui_status_refresh_conn(void);
void ui_status_refresh_nodes(void);
void ui_status_refresh_events(void);
