/**
 * @file scr_events.c
 * @brief Eventos recibidos por MQTT desde que arrancó la pantalla (el más nuevo primero).
 *        En D4 se completa con el historial de la API y el reconocimiento.
 */
#include <stdio.h>
#include "screens.h"
#include "state_store.h"
#include "theme.h"
#include "ui_api.h"
#include "ui_common.h"

static lv_obj_t *s_list, *s_empty;

void scr_events_create(lv_obj_t *p)
{
    s_list = lv_obj_create(p);
    theme_card(s_list);
    lv_obj_set_size(s_list, SCR_W - 2 * TH_GAP, SCR_H - 2 * TH_GAP);
    lv_obj_set_pos(s_list, TH_GAP, TH_GAP);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 14, 0);
    s_empty = theme_label(s_list, TH_FONT_BODY, TH_MUTED, "Sin eventos desde el arranque de la pantalla.");
}

void scr_events_refresh(uint32_t dirty)
{
    if (!(dirty & UI_DIRTY_EVENTS)) {  // horas absolutas: no hace falta el tick
        return;
    }
    static ss_event_t ev[SS_MAX_EVENTS];
    int n = state_store_events(ev, SS_MAX_EVENTS);
    for (int i = (int)lv_obj_get_child_count(s_list) - 1; i >= 0; i--) {
        lv_obj_t *ch = lv_obj_get_child(s_list, i);
        if (ch != s_empty) {
            lv_obj_delete(ch);
        }
    }
    lv_obj_set_flag(s_empty, LV_OBJ_FLAG_HIDDEN, n > 0);
    for (int i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 10, 0);
        lv_obj_set_style_pad_row(row, 4, 0);
        char hhmm[8];
        ui_hhmm(ev[i].ts, hhmm, sizeof(hhmm));
        theme_label(row, TH_FONT_BODY, TH_MUTED, hhmm);
        lv_obj_t *t = theme_label(row, TH_FONT_BODY, TH_FG, "");
        lv_label_set_text_fmt(t, "%s · %s", ui_event_title(ev[i].event_type), ev[i].node_id);
        const char *txt;
        uint32_t bg, fg;
        ui_severity_chip(ev[i].severity, &txt, &bg, &fg);
        theme_chip_set(theme_chip(row), txt, bg, fg);
        if (ev[i].detail[0]) {
            lv_obj_t *d = theme_label(row, TH_FONT_SMALL, TH_MUTED, ev[i].detail);
            lv_obj_set_width(d, LV_PCT(100));
            lv_label_set_long_mode(d, LV_LABEL_LONG_DOT);
        }
    }
}
