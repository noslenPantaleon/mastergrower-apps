/**
 * @file ui_env.h
 * @brief Piezas compartidas de las pantallas del módulo de control (Inicio, Ambiente, Cultivo,
 *        Actuadores): gráfico de temperatura y humedad de 24 h, formatos y nombres en español.
 */
#pragma once

#include <stddef.h>
#include "lvgl.h"
#include "state_store.h"

typedef struct {
    lv_obj_t *chart, *note;
    lv_chart_series_t *temp, *hum;
} ui_env_chart_t;

/** @brief Gráfico de líneas: temperatura (eje izq., 10–35 °C) y humedad (eje der., 20–100 %). */
void ui_env_chart_create(lv_obj_t *parent, int w, int h, ui_env_chart_t *out);

/** @brief Vuelve a dibujar con el historial (promedios para que entren ≤ 96 puntos). */
void ui_env_chart_update(ui_env_chart_t *c);

/** @brief "24.6" / "--" si no hay dato (NAN) o el control no está vigente. */
void ui_env_fmt(char *buf, size_t len, float v, int decimals, bool live);

const char *ui_grow_mode_title(const char *mode);
const char *ui_actuator_title(const char *id);
