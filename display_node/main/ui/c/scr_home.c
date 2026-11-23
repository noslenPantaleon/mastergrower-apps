/**
 * @file scr_home.c
 * @brief Inicio (SRS de pantalla F-01): KPI ambientales y tendencia de 24 h (módulo de control;
 *        hoy, el simulador backend/scripts/ctrl_sim.py) y la última captura de las cámaras.
 */
#include <stdio.h>
#include <string.h>
#include "screens.h"
#include "state_store.h"
#include "theme.h"
#include "ui_api.h"
#include "ui_common.h"
#include "ui_thumbs.h"
#include "ui_env.h"

#define KPI_N  4
#define KPI_W  ((SCR_W - (KPI_N + 1) * TH_GAP) / KPI_N)
#define KPI_H  96
#define ROW2_Y (TH_GAP + KPI_H + TH_GAP)
#define ROW2_H (SCR_H - ROW2_Y - TH_GAP)
#define CHART_W 456

static struct {
    lv_obj_t *kpi_value[KPI_N], *kpi_sub[KPI_N];
    lv_obj_t *cap_title, *cap_chip, *cap_when, *cap_thumb_icon, *cap_img;
    ui_env_chart_t chart;
} s_h;

static void kpi(lv_obj_t *parent, int i, const char *title, const char *unit)
{
    lv_obj_t *c = lv_obj_create(parent);
    theme_card(c);
    lv_obj_set_size(c, KPI_W, KPI_H);
    lv_obj_set_pos(c, TH_GAP + i * (KPI_W + TH_GAP), TH_GAP);
    lv_obj_set_style_pad_all(c, 12, 0);
    theme_label(c, TH_FONT_SMALL, TH_MUTED, title);
    lv_obj_t *row = lv_obj_create(c);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(row, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 6, 0);
    s_h.kpi_value[i] = theme_label(row, TH_FONT_KPI, TH_FG, "--");
    s_h.kpi_sub[i] = theme_label(row, TH_FONT_SMALL, TH_MUTED, unit);
    lv_obj_set_style_pad_bottom(s_h.kpi_sub[i], 6, 0);
}

void scr_home_create(lv_obj_t *p)
{
    kpi(p, 0, "Temperatura", "°C");
    kpi(p, 1, "Humedad", "%");
    kpi(p, 2, "CO₂", "ppm");
    kpi(p, 3, "Luz", "");

    /* Tendencia (lv_chart): vacía hasta que haya módulo de control */
    lv_obj_t *chart_card = lv_obj_create(p);
    theme_card(chart_card);
    lv_obj_set_size(chart_card, CHART_W, ROW2_H);
    lv_obj_set_pos(chart_card, TH_GAP, ROW2_Y);
    lv_obj_t *ct = theme_label(chart_card, TH_FONT_SMALL, TH_MUTED, "");
    lv_label_set_recolor(ct, true);
    lv_label_set_text(ct, "#6aa3f0 Temperatura# y #79c97b humedad# · 24 h");
    ui_env_chart_create(chart_card, CHART_W - 32, ROW2_H - 56, &s_h.chart);
    lv_obj_align(s_h.chart.chart, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* Última captura */
    lv_obj_t *cap = lv_obj_create(p);
    theme_card(cap);
    lv_obj_set_size(cap, SCR_W - CHART_W - 3 * TH_GAP, ROW2_H);
    lv_obj_set_pos(cap, CHART_W + 2 * TH_GAP, ROW2_Y);
    lv_obj_set_flex_flow(cap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cap, 10, 0);
    s_h.cap_title = theme_label(cap, TH_FONT_SMALL, TH_MUTED, "Última captura");
    lv_obj_t *thumb = lv_obj_create(cap);
    lv_obj_remove_style_all(thumb);
    lv_obj_set_size(thumb, LV_PCT(100), 112);
    lv_obj_set_style_bg_color(thumb, th_color(TH_CARD_ALT), 0);
    lv_obj_set_style_bg_opa(thumb, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(thumb, 8, 0);
    s_h.cap_thumb_icon = lv_label_create(thumb);
    lv_label_set_text(s_h.cap_thumb_icon, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_font(s_h.cap_thumb_icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_h.cap_thumb_icon, th_color(TH_MUTED), 0);
    lv_obj_center(s_h.cap_thumb_icon);
    s_h.cap_img = ui_thumb_image(thumb);
    lv_obj_t *row = lv_obj_create(cap);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    s_h.cap_chip = theme_chip(row);
    s_h.cap_when = theme_label(row, TH_FONT_SMALL, TH_MUTED, "");
}

/** Cámara con la captura más reciente (o la primera cámara si ninguna capturó). */
static bool latest_camera(ss_node_t *out)
{
    static ss_node_t nodes[SS_MAX_NODES];
    int n = state_store_nodes(nodes, SS_MAX_NODES);
    int best = -1;
    for (int i = 0; i < n; i++) {
        if (nodes[i].kind != MG_NODE_CAM) {
            continue;
        }
        if (best < 0 || nodes[i].last_capture > nodes[best].last_capture) {
            best = i;
        }
    }
    if (best >= 0) {
        *out = nodes[best];
    }
    return best >= 0;
}

static void refresh_env(uint32_t dirty)
{
    ss_env_t e;
    bool have = state_store_env(&e);
    bool live = have && state_store_env_live(&e);
    char v[16];
    ui_env_fmt(v, sizeof(v), e.temperature, 1, live);
    lv_label_set_text(s_h.kpi_value[0], v);
    ui_env_fmt(v, sizeof(v), e.humidity, 0, live);
    lv_label_set_text(s_h.kpi_value[1], v);
    ui_env_fmt(v, sizeof(v), e.co2, 0, live);
    lv_label_set_text(s_h.kpi_value[2], v);
    if (live && e.has_light) {
        lv_label_set_text(s_h.kpi_value[3], e.light_on ? "ON" : "OFF");
        lv_label_set_text_fmt(s_h.kpi_sub[3], "ciclo %s", e.light_cycle);
    } else {
        lv_label_set_text(s_h.kpi_value[3], "--");
        lv_label_set_text(s_h.kpi_sub[3], have ? "sin conexión" : "sin control");
    }
    if (dirty & UI_DIRTY_ENV) {
        ui_env_chart_update(&s_h.chart);
    }
}

void scr_home_refresh(uint32_t dirty)
{
    if (dirty & (UI_DIRTY_ENV | UI_DIRTY_TICK)) {
        refresh_env(dirty);
    }
    if (!(dirty & (UI_DIRTY_NODES | UI_DIRTY_TICK | UI_DIRTY_THUMB))) {
        return;
    }

    ss_node_t cam;
    if (!latest_camera(&cam)) {
        lv_label_set_text(s_h.cap_title, "Última captura");
        theme_chip_set(s_h.cap_chip, "Sin cámaras", TH_OFF_BG, TH_OFF_FG);
        lv_label_set_text(s_h.cap_when, "");
        return;
    }
    lv_label_set_text_fmt(s_h.cap_title, "Última captura · %s", cam.node_id);
    if (dirty & (UI_DIRTY_THUMB | UI_DIRTY_NODES)) {
        ui_thumb_bind(s_h.cap_img, cam.node_id);
    }
    thumb_info_t info;
    bool have_thumb = ui_thumb(cam.node_id, &info) != NULL;
    const char *text;
    uint32_t bg, fg;
    if (have_thumb) {
        ui_analysis_chip(&info, &text, &bg, &fg);  // con foto: el resultado del análisis
    } else {
        ui_node_chip(&cam, &text, &bg, &fg);
    }
    theme_chip_set(s_h.cap_chip, text, bg, fg);
    time_t when = have_thumb && info.ts ? info.ts : cam.last_capture;
    if (when && state_store_conn().time_valid) {
        char hhmm[8], age[32];
        ui_hhmm(when, hhmm, sizeof(hhmm));
        ui_age(time(NULL) - when, age, sizeof(age));
        lv_label_set_text_fmt(s_h.cap_when, "%s  ·  %s", hhmm, age);
    } else {
        lv_label_set_text(s_h.cap_when, when ? "" : "todavía sin capturas");
    }
}
