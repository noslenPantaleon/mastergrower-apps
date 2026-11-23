/**
 * @file scr_act.c
 * @brief Actuadores (SRS de pantalla F-03): estado de cada relé del módulo de control y si está en
 *        automático o manual. Solo lectura: el comando set_actuator todavía no está en el contrato
 *        (§4.4), así que el accionamiento manual llega con el módulo real.
 */
#include <stdio.h>
#include "screens.h"
#include "state_store.h"
#include "theme.h"
#include "ui_api.h"
#include "ui_env.h"

static lv_obj_t *s_list, *s_note;

void scr_act_create(lv_obj_t *p)
{
    lv_obj_t *card = lv_obj_create(p);
    theme_card(card);
    lv_obj_set_size(card, SCR_W - 2 * TH_GAP, SCR_H - 2 * TH_GAP);
    lv_obj_set_pos(card, TH_GAP, TH_GAP);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 10, 0);
    s_list = lv_obj_create(card);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 6, 0);
    s_note = theme_label(card, TH_FONT_SMALL, TH_MUTED,
                         "Solo lectura: el accionamiento manual llega con el módulo de control real "
                         "(comando set_actuator, Contrato §4.4).");
    lv_obj_set_width(s_note, LV_PCT(100));
    lv_label_set_long_mode(s_note, LV_LABEL_LONG_WRAP);
}

void scr_act_refresh(uint32_t dirty)
{
    if (!(dirty & (UI_DIRTY_ENV | UI_DIRTY_TICK))) {
        return;
    }
    static ss_env_t e;
    bool have = state_store_env(&e);
    bool live = have && state_store_env_live(&e);
    lv_obj_clean(s_list);
    if (!have || e.n_act == 0) {
        theme_label(s_list, TH_FONT_BODY, TH_MUTED, "Esperando al módulo de control…");
        return;
    }
    for (int i = 0; i < e.n_act; i++) {
        lv_obj_t *r = lv_obj_create(s_list);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, LV_PCT(100), 52);
        lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(r, 1, 0);
        lv_obj_set_style_border_color(r, th_color(TH_LINE), 0);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(r, 12, 0);
        lv_obj_t *n = theme_label(r, TH_FONT_TITLE, TH_FG, ui_actuator_title(e.act[i].id));
        lv_obj_set_flex_grow(n, 1);
        lv_obj_t *c = theme_chip(r);
        if (!live) {
            theme_chip_set(c, "sin dato", TH_OFF_BG, TH_OFF_FG);
        } else {
            theme_chip_set(c, e.act[i].on ? "encendido" : "apagado", e.act[i].on ? TH_OK_BG : TH_OFF_BG,
                           e.act[i].on ? TH_OK_FG : TH_OFF_FG);
        }
        theme_label(r, TH_FONT_SMALL, TH_MUTED, e.act[i].manual ? "manual" : "automático");
    }
}
