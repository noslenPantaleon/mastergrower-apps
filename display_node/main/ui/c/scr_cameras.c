/**
 * @file scr_cameras.c
 * @brief Cámaras (SRS de pantalla F-04): una tarjeta por nodo de cámara con miniatura de la última
 *        captura (API), tipo, estado, resultado del análisis local y "Capturar ahora"
 *        (capture_now con cmd_id propio; espera el ack y avisa el resultado).
 */
#include <stdio.h>
#include <string.h>
#include "command_publisher.h"
#include "esp_timer.h"
#include "screens.h"
#include "state_store.h"
#include "theme.h"
#include "ui_api.h"
#include "ui_common.h"
#include "ui_thumbs.h"

#define MAX_CARDS   8
#define COLS        3
#define CARD_W      ((SCR_W - (COLS + 1) * TH_GAP) / COLS)
#define CARD_H      248
#define RESULT_SHOW_US  (5LL * 1000000)  /**< cuánto queda visible "Captura lista" o el error */

typedef struct {
    char node_id[SS_ID_LEN];
    lv_obj_t *card, *name, *type, *chip, *when, *btn, *img, *analysis, *live;
} cam_card_t;

static struct {
    lv_obj_t *grid, *empty, *events;
    cam_card_t cards[MAX_CARDS];
    int n_cards;
} s_c;

/* ---------------------------------------------------------------- acciones */

static void on_card(lv_event_t *e)
{
    cam_card_t *c = lv_event_get_user_data(e);
    ui_shell_open_camera(c->node_id);
}

static void on_live(lv_event_t *e)
{
    cam_card_t *c = lv_event_get_user_data(e);
    ui_shell_open_live(c->node_id);
}

static void on_capture(lv_event_t *e)
{
    cam_card_t *c = lv_event_get_user_data(e);
    esp_err_t err = command_capture_now(c->node_id);
    if (err == ESP_ERR_INVALID_STATE && !state_store_conn().time_valid) {
        theme_button_set(c->btn, "Sin hora (NTP)", false);
    }
}

/* ---------------------------------------------------------------- armado */

static void card_create(cam_card_t *c, const char *node_id)
{
    strlcpy(c->node_id, node_id, sizeof(c->node_id));
    c->card = lv_obj_create(s_c.grid);
    theme_card(c->card);
    lv_obj_set_size(c->card, CARD_W, CARD_H);
    lv_obj_set_flex_flow(c->card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c->card, 6, 0);
    /* Tocar la tarjeta (fuera de los botones) abre el detalle */
    lv_obj_add_flag(c->card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(c->card, th_color(TH_CARD_ALT), LV_STATE_PRESSED);
    lv_obj_add_event_cb(c->card, on_card, LV_EVENT_CLICKED, c);

    lv_obj_t *thumb = lv_obj_create(c->card);
    lv_obj_remove_style_all(thumb);
    lv_obj_set_size(thumb, LV_PCT(100), 84);
    lv_obj_set_style_bg_color(thumb, th_color(TH_CARD_ALT), 0);
    lv_obj_set_style_bg_opa(thumb, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(thumb, 8, 0);
    lv_obj_t *icon = lv_label_create(thumb);
    lv_label_set_text(icon, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(icon, th_color(TH_MUTED), 0);
    lv_obj_center(icon);
    c->img = ui_thumb_image(thumb);
    c->analysis = theme_chip(thumb);
    lv_obj_align(c->analysis, LV_ALIGN_BOTTOM_LEFT, 6, -6);
    lv_obj_add_flag(c->analysis, LV_OBJ_FLAG_HIDDEN);
    c->live = lv_button_create(thumb);
    lv_obj_set_size(c->live, LV_SIZE_CONTENT, 32);
    lv_obj_set_style_pad_hor(c->live, 10, 0);
    lv_obj_set_style_radius(c->live, 16, 0);
    lv_obj_set_style_shadow_width(c->live, 0, 0);
    lv_obj_set_style_bg_color(c->live, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(c->live, LV_OPA_60, 0);
    lv_obj_set_style_bg_opa(c->live, LV_OPA_30, LV_STATE_DISABLED);
    lv_obj_align(c->live, LV_ALIGN_TOP_RIGHT, -6, 6);
    lv_obj_t *ll = theme_label(c->live, TH_FONT_SMALL, TH_FG, LV_SYMBOL_PLAY " En vivo");
    lv_obj_center(ll);
    lv_obj_add_event_cb(c->live, on_live, LV_EVENT_CLICKED, c);

    c->name = theme_label(c->card, TH_FONT_TITLE, TH_FG, node_id);
    c->type = theme_label(c->card, TH_FONT_SMALL, TH_MUTED, "");
    lv_obj_t *row = lv_obj_create(c->card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    c->chip = theme_chip(row);
    c->when = theme_label(row, TH_FONT_SMALL, TH_MUTED, "");

    c->btn = theme_button(c->card, "Capturar ahora");
    lv_obj_set_width(c->btn, LV_PCT(100));
    lv_obj_set_style_margin_top(c->btn, 4, 0);
    lv_obj_add_event_cb(c->btn, on_capture, LV_EVENT_CLICKED, c);
}

void scr_cameras_create(lv_obj_t *p)
{
    lv_obj_t *col = lv_obj_create(p);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, SCR_W, SCR_H);
    lv_obj_set_style_pad_all(col, TH_GAP, 0);
    lv_obj_set_style_pad_row(col, TH_GAP, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(col, LV_DIR_VER);

    s_c.grid = lv_obj_create(col);
    lv_obj_remove_style_all(s_c.grid);
    lv_obj_set_size(s_c.grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_c.grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(s_c.grid, TH_GAP, 0);
    lv_obj_set_style_pad_row(s_c.grid, TH_GAP, 0);
    s_c.empty = theme_label(s_c.grid, TH_FONT_BODY, TH_MUTED, "Esperando datos de las cámaras…");

    s_c.events = lv_obj_create(col);
    theme_card(s_c.events);
    lv_obj_set_size(s_c.events, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_c.events, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_c.events, 8, 0);
    theme_label(s_c.events, TH_FONT_SMALL, TH_MUTED, "Últimos eventos");
}

/* ---------------------------------------------------------------- refresco */

static void button_state(cam_card_t *c, bool online)
{
    cmd_status_t st = command_status(c->node_id);
    bool recent = esp_timer_get_time() - st.updated_us < RESULT_SHOW_US;
    char txt[48];
    switch (st.state) {
    case CMD_SENT:
    case CMD_ACCEPTED:
        theme_button_set(c->btn, "Capturando…", false);
        return;
    case CMD_DONE:
        if (recent) {
            theme_button_set(c->btn, LV_SYMBOL_OK " Captura lista", false);
            return;
        }
        break;
    case CMD_REJECTED:
        if (recent) {
            snprintf(txt, sizeof(txt), "Rechazado: %s", st.reason);
            theme_button_set(c->btn, txt, false);
            return;
        }
        break;
    case CMD_TIMEOUT:
        if (recent) {
            theme_button_set(c->btn, "Sin respuesta", false);
            return;
        }
        break;
    default:
        break;
    }
    theme_button_set(c->btn, "Capturar ahora", online && state_store_conn().mqtt);
}

static void card_refresh(cam_card_t *c, const ss_node_t *n)
{
    lv_label_set_text(c->type, n->camera_type[0] ? n->camera_type : "-");
    const char *text;
    uint32_t bg, fg;
    ui_node_chip(n, &text, &bg, &fg);
    theme_chip_set(c->chip, text, bg, fg);
    if (n->last_capture) {
        char hhmm[8];
        ui_hhmm(n->last_capture, hhmm, sizeof(hhmm));
        lv_label_set_text_fmt(c->when, "captura %s", hhmm);
    } else {
        lv_label_set_text(c->when, "sin capturas");
    }
    button_state(c, ui_node_online(n));
    lv_obj_set_state(c->live, LV_STATE_DISABLED, !ui_node_online(n));
}

static void card_thumb(cam_card_t *c)
{
    ui_thumb_bind(c->img, c->node_id);
    thumb_info_t info;
    if (ui_thumb(c->node_id, &info)) {
        const char *text;
        uint32_t bg, fg;
        ui_analysis_chip(&info, &text, &bg, &fg);
        theme_chip_set(c->analysis, text, bg, fg);
        lv_obj_remove_flag(c->analysis, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(c->analysis);
    }
    lv_obj_move_foreground(c->live);
}

static void events_refresh(void)
{
    for (int i = (int)lv_obj_get_child_count(s_c.events) - 1; i >= 1; i--) {
        lv_obj_delete(lv_obj_get_child(s_c.events, i));
    }
    static ss_event_t ev[4];
    int n = state_store_events(ev, 4);
    if (n == 0) {
        theme_label(s_c.events, TH_FONT_BODY, TH_MUTED, "Sin eventos recientes.");
    }
    for (int i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_create(s_c.events);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_t *t = theme_label(row, TH_FONT_BODY, TH_FG, "");
        lv_label_set_text_fmt(t, "•  %s · %s", ui_event_title(ev[i].event_type), ev[i].node_id);
        const char *txt;
        uint32_t bg, fg;
        ui_severity_chip(ev[i].severity, &txt, &bg, &fg);
        theme_chip_set(theme_chip(row), txt, bg, fg);
    }
}

void scr_cameras_refresh(uint32_t dirty)
{
    if (dirty & UI_DIRTY_EVENTS) {
        events_refresh();
    }
    if (!(dirty & (UI_DIRTY_NODES | UI_DIRTY_CMD | UI_DIRTY_TICK | UI_DIRTY_CONN | UI_DIRTY_THUMB))) {
        return;
    }
    static ss_node_t nodes[SS_MAX_NODES];
    int n = state_store_nodes(nodes, SS_MAX_NODES);
    for (int i = 0; i < n; i++) {
        if (nodes[i].kind != MG_NODE_CAM) {
            continue;
        }
        cam_card_t *c = NULL;
        for (int k = 0; k < s_c.n_cards && !c; k++) {
            if (strcmp(s_c.cards[k].node_id, nodes[i].node_id) == 0) {
                c = &s_c.cards[k];
            }
        }
        bool is_new = false;
        if (!c && s_c.n_cards < MAX_CARDS) {  // cámara nueva: tarjeta nueva (las cámaras no se quitan)
            c = &s_c.cards[s_c.n_cards++];
            card_create(c, nodes[i].node_id);
            is_new = true;
        }
        if (c) {
            card_refresh(c, &nodes[i]);
            if (is_new || (dirty & UI_DIRTY_THUMB)) {
                card_thumb(c);
            }
        }
    }
    lv_obj_set_flag(s_c.empty, LV_OBJ_FLAG_HIDDEN, s_c.n_cards > 0);
}
