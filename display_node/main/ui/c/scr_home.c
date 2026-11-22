/**
 * @file scr_home.c
 * @brief Inicio (SRS de pantalla F-01): KPI ambientales, tendencia de 24 h, última captura.
 *        Las variables ambientales llegan con el módulo de control (Contrato §4.4): hasta
 *        entonces los KPI muestran "--" y lo dicen.
 */
#include <stdio.h>
#include <string.h>
#include "screens.h"
#include "state_store.h"
#include "theme.h"
#include "ui_api.h"
#include "ui_common.h"

#define KPI_N  4
#define KPI_W  ((SCR_W - (KPI_N + 1) * TH_GAP) / KPI_N)
#define KPI_H  96
#define ROW2_Y (TH_GAP + KPI_H + TH_GAP)
#define ROW2_H (SCR_H - ROW2_Y - TH_GAP)
#define CHART_W 456

static struct {
    lv_obj_t *kpi_value[KPI_N], *kpi_sub[KPI_N];
    lv_obj_t *cap_title, *cap_chip, *cap_when, *cap_thumb_icon;
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
    kpi(p, 2, "CO2", "ppm");
    kpi(p, 3, "Luz", "");

    /* Tendencia (lv_chart): vacía hasta que haya módulo de control */
    lv_obj_t *chart_card = lv_obj_create(p);
    theme_card(chart_card);
    lv_obj_set_size(chart_card, CHART_W, ROW2_H);
    lv_obj_set_pos(chart_card, TH_GAP, ROW2_Y);
    theme_label(chart_card, TH_FONT_SMALL, TH_MUTED, "Temperatura y humedad · 24 h");
    lv_obj_t *chart = lv_chart_create(chart_card);
    lv_obj_set_size(chart, CHART_W - 32, ROW2_H - 56);
    lv_obj_align(chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_line_color(chart, th_color(TH_LINE), LV_PART_MAIN);
    lv_chart_add_series(chart, th_color(TH_SERIES_1), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_add_series(chart, th_color(TH_SERIES_2), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_t *note = theme_label(chart, TH_FONT_SMALL, TH_MUTED, "Sin datos del módulo de control");
    lv_obj_center(note);

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

void scr_home_refresh(uint32_t dirty)
{
    if (!(dirty & (UI_DIRTY_NODES | UI_DIRTY_TICK))) {
        return;
    }
    /* KPI: sin módulo de control todavía */
    for (int i = 0; i < KPI_N; i++) {
        lv_label_set_text(s_h.kpi_value[i], "--");
    }
    lv_label_set_text(s_h.kpi_sub[3], "sin control");

    ss_node_t cam;
    if (!latest_camera(&cam)) {
        lv_label_set_text(s_h.cap_title, "Última captura");
        theme_chip_set(s_h.cap_chip, "Sin cámaras", TH_OFF_BG, TH_OFF_FG);
        lv_label_set_text(s_h.cap_when, "");
        return;
    }
    lv_label_set_text_fmt(s_h.cap_title, "Última captura · %s", cam.node_id);
    const char *text;
    uint32_t bg, fg;
    ui_node_chip(&cam, &text, &bg, &fg);
    theme_chip_set(s_h.cap_chip, text, bg, fg);
    if (cam.last_capture && state_store_conn().time_valid) {
        char hhmm[8], age[32];
        ui_hhmm(cam.last_capture, hhmm, sizeof(hhmm));
        ui_age(time(NULL) - cam.last_capture, age, sizeof(age));
        lv_label_set_text_fmt(s_h.cap_when, "%s  ·  %s", hhmm, age);
    } else {
        lv_label_set_text(s_h.cap_when, cam.last_capture ? "" : "todavía sin capturas");
    }
}
