/**
 * @file ui_status.c
 * @brief Ver ui_status.h.
 *
 * Textos sin tildes por ahora: las fuentes Montserrat de LVGL solo traen ASCII. En D2 se agregan
 * fuentes propias con Latin-1.
 */
#include "ui_status.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "lvgl.h"
#include "sdkconfig.h"
#include "state_store.h"

#define C_BG      0x121512
#define C_CARD    0x1b201a
#define C_LINE    0x2b3229
#define C_FG      0xe6ebe4
#define C_MUTED   0x94a08f
#define C_OK      0x7cc47f
#define C_WARN    0xe0b050
#define C_BAD     0xe06c5a
#define C_STALE   0x6b7468

#define MAX_EVENTS_SHOWN 8

static struct {
    lv_obj_t *conn, *clock, *nodes, *events, *nodes_empty;
} s_ui;

/* ---------------------------------------------------------------- utilidades */

static void local_hhmm(time_t t, char *buf, size_t len)
{
    t += (time_t)CONFIG_MG_UTC_OFFSET_MIN * 60;
    struct tm tm;
    gmtime_r(&t, &tm);
    snprintf(buf, len, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

static void age_text(int64_t s64, char *buf, size_t len)
{
    int s = s64 > 999999999 ? 999999999 : (int)s64;
    if (s < 60) {
        snprintf(buf, len, "hace %ds", s);
    } else if (s < 3600) {
        snprintf(buf, len, "hace %d min", s / 60);
    } else if (s < 86400) {
        snprintf(buf, len, "hace %d h", s / 3600);
    } else {
        snprintf(buf, len, "hace %d d", s / 86400);
    }
}

static uint32_t state_color(const ss_node_t *n)
{
    if (state_store_is_stale(n)) {
        return C_STALE;
    }
    if (strcmp(n->node_state, "operational") == 0) {
        return C_OK;
    }
    if (strcmp(n->node_state, "booting") == 0 || strcmp(n->node_state, "autonomous") == 0) {
        return C_WARN;
    }
    return C_BAD;  // error, offline
}

static uint32_t severity_color(const char *sev)
{
    return strcmp(sev, "high") == 0 ? C_BAD : strcmp(sev, "medium") == 0 ? C_WARN : C_MUTED;
}

static lv_obj_t *card(lv_obj_t *parent, const char *title, int x, int w)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, 72);
    lv_obj_set_size(c, w, 480 - 72 - 16);
    lv_obj_set_style_bg_color(c, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_border_color(c, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_pad_all(c, 16, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 10, 0);
    lv_obj_t *t = lv_label_create(c);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, lv_color_hex(C_MUTED), 0);
    return c;
}

static lv_obj_t *row(lv_obj_t *parent)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(r, 10, 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_USER_1);  // marca: fila dinámica (se borra al redibujar)
    return r;
}

static void dot(lv_obj_t *parent, uint32_t color)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, 12, 12);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(color), 0);
    lv_obj_set_style_margin_top(d, 6, 0);
}

static void clear_rows(lv_obj_t *card_obj)
{
    for (int i = (int)lv_obj_get_child_count(card_obj) - 1; i >= 0; i--) {
        lv_obj_t *ch = lv_obj_get_child(card_obj, i);
        if (lv_obj_has_flag(ch, LV_OBJ_FLAG_USER_1)) {
            lv_obj_delete(ch);
        }
    }
}

/* ---------------------------------------------------------------- refresco */

void ui_status_refresh_conn(void)
{
    ss_conn_t c = state_store_conn();
    lv_label_set_text_fmt(s_ui.conn, "%s WiFi   %s MQTT", c.wifi ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE,
                          c.mqtt ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(s_ui.conn, lv_color_hex(c.wifi && c.mqtt ? C_OK : C_BAD), 0);
    if (c.time_valid) {
        char hhmm[8];
        local_hhmm(time(NULL), hhmm, sizeof(hhmm));
        lv_label_set_text(s_ui.clock, hhmm);
    } else {
        lv_label_set_text(s_ui.clock, "--:--");
    }
}

void ui_status_refresh_nodes(void)
{
    static ss_node_t nodes[SS_MAX_NODES];
    int n = state_store_nodes(nodes, SS_MAX_NODES);
    clear_rows(s_ui.nodes);
    lv_obj_set_flag(s_ui.nodes_empty, LV_OBJ_FLAG_HIDDEN, n > 0);
    for (int i = 0; i < n; i++) {
        const ss_node_t *nd = &nodes[i];
        bool stale = state_store_is_stale(nd);
        lv_obj_t *r = row(s_ui.nodes);
        dot(r, state_color(nd));
        lv_obj_t *col = lv_obj_create(r);
        lv_obj_remove_style_all(col);
        lv_obj_set_size(col, LV_PCT(90), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);

        lv_obj_t *name = lv_label_create(col);
        lv_label_set_text_fmt(name, "%s   %s%s", nd->node_id, nd->node_state,
                              stale ? "  (dato antiguo)" : "");
        lv_obj_set_style_text_color(name, lv_color_hex(stale ? C_STALE : C_FG), 0);

        char detail[160], age[32], cap[16] = "-";
        age_text(state_store_age_s(nd), age, sizeof(age));
        if (nd->last_capture) {
            local_hhmm(nd->last_capture, cap, sizeof(cap));
        }
        int len = snprintf(detail, sizeof(detail), "%s", age);
        if (nd->rssi) {
            len += snprintf(detail + len, sizeof(detail) - len, "  |  RSSI %d dBm", nd->rssi);
        }
        if (nd->sd_free_pct >= 0) {
            len += snprintf(detail + len, sizeof(detail) - len, "  |  SD %d%%", nd->sd_free_pct);
        }
        if (nd->kind == MG_NODE_CAM) {
            snprintf(detail + len, sizeof(detail) - len, "  |  captura %s", cap);
        }
        lv_obj_t *d = lv_label_create(col);
        lv_label_set_text(d, detail);
        lv_obj_set_style_text_font(d, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(d, lv_color_hex(C_MUTED), 0);
    }
}

void ui_status_refresh_events(void)
{
    static ss_event_t ev[MAX_EVENTS_SHOWN];
    int n = state_store_events(ev, MAX_EVENTS_SHOWN);
    clear_rows(s_ui.events);
    for (int i = 0; i < n; i++) {
        lv_obj_t *r = row(s_ui.events);
        dot(r, severity_color(ev[i].severity));
        lv_obj_t *col = lv_obj_create(r);
        lv_obj_remove_style_all(col);
        lv_obj_set_size(col, LV_PCT(90), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        char hhmm[8] = "--:--";
        if (ev[i].ts) {
            local_hhmm(ev[i].ts, hhmm, sizeof(hhmm));
        }
        lv_obj_t *t = lv_label_create(col);
        lv_label_set_text_fmt(t, "%s  %s  %s", hhmm, ev[i].node_id, ev[i].event_type);
        if (ev[i].detail[0]) {
            lv_obj_t *d = lv_label_create(col);
            lv_label_set_long_mode(d, LV_LABEL_LONG_DOT);
            lv_obj_set_width(d, LV_PCT(100));
            lv_label_set_text(d, ev[i].detail);
            lv_obj_set_style_text_font(d, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(d, lv_color_hex(C_MUTED), 0);
        }
    }
}

/* ---------------------------------------------------------------- armado */

void ui_status_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(C_FG), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "MasterGrow");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(title, 16, 18);

    s_ui.clock = lv_label_create(scr);
    lv_obj_set_style_text_font(s_ui.clock, &lv_font_montserrat_28, 0);
    lv_obj_align(s_ui.clock, LV_ALIGN_TOP_RIGHT, -16, 18);

    s_ui.conn = lv_label_create(scr);
    lv_obj_align(s_ui.conn, LV_ALIGN_TOP_RIGHT, -120, 24);

    s_ui.nodes = card(scr, "NODOS", 16, 440);
    s_ui.nodes_empty = lv_label_create(s_ui.nodes);
    lv_label_set_text(s_ui.nodes_empty, "Esperando datos de los nodos...");
    lv_obj_set_style_text_color(s_ui.nodes_empty, lv_color_hex(C_MUTED), 0);
    s_ui.events = card(scr, "EVENTOS RECIENTES", 16 + 440 + 16, 800 - 440 - 48);

    ui_status_refresh_conn();
    ui_status_refresh_nodes();
    ui_status_refresh_events();
}
