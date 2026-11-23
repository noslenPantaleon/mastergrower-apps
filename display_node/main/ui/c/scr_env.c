/**
 * @file scr_env.c
 * @brief Ambiente (SRS de pantalla F-02): tendencia de 24 h de temperatura y humedad y los valores
 *        actuales del módulo de control (hoy, el simulador).
 */
#include <stdio.h>
#include "screens.h"
#include "state_store.h"
#include "theme.h"
#include "ui_api.h"
#include "ui_common.h"
#include "ui_env.h"

#define CHART_W 488

static struct {
    ui_env_chart_t chart;
    lv_obj_t *val[5], *updated, *src;
} s_e;

static const char *NAMES[5] = { "Temperatura", "Humedad", "CO₂", "Luz", "Ciclo de luz" };

void scr_env_create(lv_obj_t *p)
{
    lv_obj_t *card = lv_obj_create(p);
    theme_card(card);
    lv_obj_set_size(card, CHART_W + 32, SCR_H - 2 * TH_GAP);
    lv_obj_set_pos(card, TH_GAP, TH_GAP);
    lv_obj_t *t = theme_label(card, TH_FONT_SMALL, TH_MUTED, "");
    lv_label_set_recolor(t, true);
    lv_label_set_text(t, "#6aa3f0 Temperatura (°C, izq.)#  ·  #79c97b Humedad (%, der.)#  ·  últimas 24 h");
    ui_env_chart_create(card, CHART_W, SCR_H - 2 * TH_GAP - 60, &s_e.chart);
    lv_obj_align(s_e.chart.chart, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *side = lv_obj_create(p);
    theme_card(side);
    lv_obj_set_size(side, SCR_W - CHART_W - 32 - 3 * TH_GAP, SCR_H - 2 * TH_GAP);
    lv_obj_set_pos(side, CHART_W + 32 + 2 * TH_GAP, TH_GAP);
    lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(side, 4, 0);
    for (int i = 0; i < 5; i++) {
        theme_label(side, TH_FONT_SMALL, TH_MUTED, NAMES[i]);
        s_e.val[i] = theme_label(side, TH_FONT_TITLE, TH_FG, "--");
    }
    s_e.updated = theme_label(side, TH_FONT_SMALL, TH_MUTED, "");
    lv_obj_set_style_margin_top(s_e.updated, 6, 0);
    s_e.src = theme_label(side, TH_FONT_SMALL, TH_MUTED, "");
}

void scr_env_refresh(uint32_t dirty)
{
    if (!(dirty & (UI_DIRTY_ENV | UI_DIRTY_TICK))) {
        return;
    }
    ss_env_t e;
    bool have = state_store_env(&e);
    bool live = have && state_store_env_live(&e);
    char v[24], buf[40];
    ui_env_fmt(v, sizeof(v), e.temperature, 1, live);
    lv_label_set_text_fmt(s_e.val[0], "%s °C", v);
    ui_env_fmt(v, sizeof(v), e.humidity, 0, live);
    lv_label_set_text_fmt(s_e.val[1], "%s %%", v);
    ui_env_fmt(v, sizeof(v), e.co2, 0, live);
    lv_label_set_text_fmt(s_e.val[2], "%s ppm", v);
    ui_env_fmt(v, sizeof(v), e.lux, 0, live);
    lv_label_set_text_fmt(s_e.val[3], "%s lx%s", v, live && e.has_light ? (e.light_on ? " · ON" : " · OFF") : "");
    lv_label_set_text(s_e.val[4], live && e.light_cycle[0] ? e.light_cycle : "--");
    if (have && e.ts && state_store_conn().time_valid) {
        ui_age(time(NULL) - e.ts, buf, sizeof(buf));
        lv_label_set_text_fmt(s_e.updated, "Actualizado %s", buf);
    } else {
        lv_label_set_text(s_e.updated, have ? "" : "Esperando al módulo de control…");
    }
    lv_label_set_text_fmt(s_e.src, "%s%s", have ? e.node_id : "", have && !live ? " · sin conexión" : "");
    if (dirty & UI_DIRTY_ENV || dirty == UI_DIRTY_ALL) {
        ui_env_chart_update(&s_e.chart);
    }
}
