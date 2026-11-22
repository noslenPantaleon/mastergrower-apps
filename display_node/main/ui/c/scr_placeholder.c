/**
 * @file scr_placeholder.c
 * @brief Pantallas que todavía no tienen datos: explican qué falta en vez de quedar vacías.
 */
#include "screens.h"
#include "theme.h"

void scr_placeholder_create(lv_obj_t *parent, const char *title, const char *why)
{
    lv_obj_t *c = lv_obj_create(parent);
    theme_card(c);
    lv_obj_set_size(c, SCR_W - 2 * TH_GAP, SCR_H - 2 * TH_GAP);
    lv_obj_set_pos(c, TH_GAP, TH_GAP);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(c, 12, 0);
    theme_label(c, TH_FONT_H2, TH_FG, title);
    lv_obj_t *l = theme_label(c, TH_FONT_BODY, TH_MUTED, why);
    lv_obj_set_width(l, 520);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
}
