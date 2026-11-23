/**
 * @file scr_grow.c
 * @brief Cultivo (SRS de pantalla F-02): las variables de la zona de raíces según el modo de
 *        cultivo (SRS de control F-12): sustrato y fibra de coco → humedad y EC del sustrato;
 *        hidroponía DWC → pH, EC, oxígeno disuelto y temperatura del agua; siempre, el reservorio.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "screens.h"
#include "state_store.h"
#include "theme.h"
#include "ui_api.h"
#include "ui_env.h"

#define TILE_W ((SCR_W - 4 * TH_GAP) / 3)
#define TILE_H 128
#define TILES  6

static struct {
    lv_obj_t *title, *tile[TILES], *name[TILES], *value[TILES], *bar[TILES];
} s_g;

static void tile_create(lv_obj_t *grid, int i)
{
    lv_obj_t *t = lv_obj_create(grid);
    theme_card(t);
    lv_obj_set_size(t, TILE_W, TILE_H);
    lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(t, 6, 0);
    s_g.name[i] = theme_label(t, TH_FONT_SMALL, TH_MUTED, "");
    s_g.value[i] = theme_label(t, TH_FONT_H2, TH_FG, "--");
    s_g.bar[i] = lv_bar_create(t);
    lv_obj_set_size(s_g.bar[i], LV_PCT(100), 8);
    lv_bar_set_range(s_g.bar[i], 0, 100);
    lv_obj_set_style_bg_color(s_g.bar[i], th_color(TH_CARD_ALT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_g.bar[i], th_color(TH_SERIES_2), LV_PART_INDICATOR);
    s_g.tile[i] = t;
}

void scr_grow_create(lv_obj_t *p)
{
    lv_obj_t *col = lv_obj_create(p);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, SCR_W, SCR_H);
    lv_obj_set_style_pad_all(col, TH_GAP, 0);
    lv_obj_set_style_pad_row(col, 12, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    s_g.title = theme_label(col, TH_FONT_TITLE, TH_FG, "Esperando al módulo de control…");
    lv_obj_t *grid = lv_obj_create(col);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, TH_GAP, 0);
    lv_obj_set_style_pad_row(grid, TH_GAP, 0);
    for (int i = 0; i < TILES; i++) {
        tile_create(grid, i);
    }
}

/** Una tarjeta: nombre, valor con unidad y barra (pct < 0 = sin barra). */
static int tile(int i, const char *name, float v, int dec, const char *unit, float pct, bool live)
{
    char num[16];
    ui_env_fmt(num, sizeof(num), v, dec, live);
    lv_label_set_text(s_g.name[i], name);
    lv_label_set_text_fmt(s_g.value[i], "%s %s", num, unit);
    bool bar = pct >= 0 && live && !isnan(v);
    lv_obj_set_flag(s_g.bar[i], LV_OBJ_FLAG_HIDDEN, !bar);
    if (bar) {
        lv_bar_set_value(s_g.bar[i], (int32_t)lroundf(pct), LV_ANIM_OFF);
    }
    lv_obj_remove_flag(s_g.tile[i], LV_OBJ_FLAG_HIDDEN);
    return i + 1;
}

void scr_grow_refresh(uint32_t dirty)
{
    if (!(dirty & (UI_DIRTY_ENV | UI_DIRTY_TICK))) {
        return;
    }
    ss_env_t e;
    bool have = state_store_env(&e);
    bool live = have && state_store_env_live(&e);
    if (!have) {
        for (int i = 0; i < TILES; i++) {
            lv_obj_add_flag(s_g.tile[i], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    lv_label_set_text_fmt(s_g.title, "Modo de cultivo: %s%s", ui_grow_mode_title(e.grow_mode),
                          live ? "" : "  (sin conexión)");
    int n = 0;
    if (strcmp(e.grow_mode, "dwc") == 0) {
        n = tile(n, "pH de la solución", e.ph, 2, "", (e.ph - 4) / 4 * 100, live);  // barra 4–8
        n = tile(n, "EC de la solución", e.ec, 2, "mS/cm", e.ec / 3 * 100, live);
        n = tile(n, "Oxígeno disuelto", e.dissolved_oxygen, 1, "mg/L", e.dissolved_oxygen / 10 * 100, live);
        n = tile(n, "Temperatura del agua", e.water_temperature, 1, "°C", -1, live);
    } else {
        n = tile(n, "Humedad del sustrato", e.substrate_moisture, 0, "%", e.substrate_moisture, live);
        n = tile(n, "EC del sustrato", e.substrate_ec, 2, "mS/cm", e.substrate_ec / 3 * 100, live);
    }
    n = tile(n, "Nivel del reservorio", e.reservoir_level, 0, "%", e.reservoir_level, live);
    for (int i = n; i < TILES; i++) {
        lv_obj_add_flag(s_g.tile[i], LV_OBJ_FLAG_HIDDEN);
    }
}
