/**
 * @file ui_env.c
 * @brief Ver ui_env.h.
 */
#include "ui_env.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "theme.h"

#define MAX_POINTS 96

void ui_env_chart_create(lv_obj_t *parent, int w, int h, ui_env_chart_t *out)
{
    lv_obj_t *c = lv_chart_create(parent);
    lv_obj_set_size(c, w, h);
    lv_chart_set_type(c, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(c, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(c, 4, 0);
    lv_chart_set_axis_range(c, LV_CHART_AXIS_PRIMARY_Y, 100, 350);   // °C × 10
    lv_chart_set_axis_range(c, LV_CHART_AXIS_SECONDARY_Y, 20, 100);  // % HR
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 4, 0);
    lv_obj_set_style_line_color(c, th_color(TH_LINE), LV_PART_MAIN);
    lv_obj_set_style_line_width(c, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(c, 0, 0, LV_PART_INDICATOR);  // sin puntos: solo la línea
    out->chart = c;
    out->temp = lv_chart_add_series(c, th_color(TH_SERIES_1), LV_CHART_AXIS_PRIMARY_Y);
    out->hum = lv_chart_add_series(c, th_color(TH_SERIES_2), LV_CHART_AXIS_SECONDARY_Y);
    out->note = theme_label(c, TH_FONT_SMALL, TH_MUTED, "Sin datos del módulo de control");
    lv_obj_center(out->note);
    lv_chart_set_point_count(c, 2);
    lv_chart_set_all_values(c, out->temp, LV_CHART_POINT_NONE);
    lv_chart_set_all_values(c, out->hum, LV_CHART_POINT_NONE);
}

void ui_env_chart_update(ui_env_chart_t *c)
{
    static ss_sample_t *h;
    if (!h) {
        h = heap_caps_malloc(SS_HIST_MAX * sizeof(ss_sample_t), MALLOC_CAP_SPIRAM);
        if (!h) {
            return;
        }
    }
    int n = state_store_env_history(h, SS_HIST_MAX);
    if (n < 2) {
        lv_label_set_text(c->note, n ? "Juntando datos… (un punto por minuto)" : "Sin datos del módulo de control");
        lv_obj_remove_flag(c->note, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(c->note, LV_OBJ_FLAG_HIDDEN);
    int bucket = (n + MAX_POINTS - 1) / MAX_POINTS;
    int points = (n + bucket - 1) / bucket;
    lv_chart_set_point_count(c->chart, points);
    for (int p = 0; p < points; p++) {
        double t = 0, hu = 0;
        int k = 0;
        for (int i = p * bucket; i < n && i < (p + 1) * bucket; i++, k++) {
            t += h[i].temperature;
            hu += h[i].humidity;
        }
        lv_chart_set_series_value_by_id(c->chart, c->temp, p, (int32_t)lround(t / k * 10));
        lv_chart_set_series_value_by_id(c->chart, c->hum, p, (int32_t)lround(hu / k));
    }
    lv_chart_refresh(c->chart);
}

void ui_env_fmt(char *buf, size_t len, float v, int decimals, bool live)
{
    if (!live || isnan(v)) {
        snprintf(buf, len, "--");
    } else {
        snprintf(buf, len, "%.*f", decimals, (double)v);
    }
}

const char *ui_grow_mode_title(const char *m)
{
    return strcmp(m, "substrate") == 0 ? "Sustrato"
         : strcmp(m, "coco_coir") == 0 ? "Fibra de coco con goteo"
         : strcmp(m, "dwc") == 0 ? "Hidroponía DWC" : (m[0] ? m : "-");
}

const char *ui_actuator_title(const char *id)
{
    return strcmp(id, "light") == 0 ? "Luces LED"
         : strcmp(id, "fan") == 0 ? "Ventilación"
         : strcmp(id, "water_pump") == 0 ? "Bomba de riego"
         : strcmp(id, "air_pump") == 0 ? "Bomba de aire" : id;
}
