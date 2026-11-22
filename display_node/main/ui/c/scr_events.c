/**
 * @file scr_events.c
 * @brief Eventos y alarmas (SRS de pantalla F-06): historial de la API (GET /events), el más nuevo
 *        primero, con severidad, estado y "Reconocer" (PATCH event_state=acknowledged). Se recarga
 *        al entrar y cuando llega un evento por MQTT.
 */
#include <stdio.h>
#include <string.h>
#include "api_jobs.h"
#include "cJSON.h"
#include "screens.h"
#include "theme.h"
#include "ui_api.h"
#include "ui_common.h"

#define MAX_ROWS   40
#define LIST_BODY  (24 * 1024)

static struct {
    lv_obj_t *list, *status, *btn_open, *btn_all;
    bool only_open;
    bool loading, reload_pending;
    uint32_t gen;
    char ids[MAX_ROWS][16];
} s_ev = { .only_open = true };

static void load(void);

static void state_chip(lv_obj_t *row, const char *state)
{
    lv_obj_t *c = theme_chip(row);
    if (strcmp(state, "open") == 0) {
        theme_chip_set(c, "abierto", TH_BAD_BG, TH_BAD_FG);
    } else if (strcmp(state, "acknowledged") == 0) {
        theme_chip_set(c, "reconocido", TH_OFF_BG, TH_OFF_FG);
    } else {
        theme_chip_set(c, "resuelto", TH_OK_BG, TH_OK_FG);
    }
}

static void on_ack_done(api_result_t *r, void *ctx)
{
    if (r->status != 200) {
        lv_label_set_text_fmt(s_ev.status, "No se pudo reconocer (HTTP %d)", r->status);
    }
    load();
    ui_shell_alarms_changed();
}

static void on_ack(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    char path[48];
    snprintf(path, sizeof(path), "/events/%s", s_ev.ids[i]);
    lv_obj_t *btn = lv_event_get_target(e);
    theme_button_set(btn, "…", false);
    api_send("PATCH", path, "{\"event_state\":\"acknowledged\"}", on_ack_done, NULL);
}

static void clear_rows(void)
{
    lv_obj_clean(s_ev.list);
}

static void add_row(int i, const cJSON *ev)
{
    const char *type = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(ev, "event_type"));
    const char *node = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(ev, "node_id"));
    const char *sev = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(ev, "severity"));
    const char *state = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(ev, "event_state"));
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(ev, "event_id"));
    const char *started = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(ev, "started_at"));
    const cJSON *detail = cJSON_GetObjectItemCaseSensitive(ev, "detail");
    if (!type || !node || !sev || !state || !id) {
        return;
    }
    strlcpy(s_ev.ids[i], id, sizeof(s_ev.ids[i]));

    lv_obj_t *row = lv_obj_create(s_ev.list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(row, 8, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, th_color(TH_LINE), 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_style_pad_row(row, 4, 0);

    char when[20];
    ui_ddmm_hhmm(ui_parse_iso(started), when, sizeof(when));
    theme_label(row, TH_FONT_SMALL, TH_MUTED, when);
    lv_obj_t *t = theme_label(row, TH_FONT_BODY, TH_FG, "");
    lv_label_set_text_fmt(t, "%s · %s", ui_event_title(type), node);
    const char *txt;
    uint32_t bg, fg;
    ui_severity_chip(sev, &txt, &bg, &fg);
    theme_chip_set(theme_chip(row), txt, bg, fg);
    state_chip(row, state);
    if (strcmp(state, "open") == 0) {
        lv_obj_t *b = theme_button_secondary(row, "Reconocer");
        lv_obj_set_size(b, LV_SIZE_CONTENT, 36);
        lv_obj_set_style_pad_hor(b, 12, 0);
        lv_obj_add_event_cb(b, on_ack, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    if (cJSON_IsString(detail) && detail->valuestring[0]) {
        lv_obj_t *d = theme_label(row, TH_FONT_SMALL, TH_MUTED, detail->valuestring);
        lv_obj_set_width(d, LV_PCT(100));
        lv_label_set_long_mode(d, LV_LABEL_LONG_DOT);
    }
}

static void on_loaded(api_result_t *r, void *ctx)
{
    s_ev.loading = false;
    if ((uint32_t)(uintptr_t)ctx != s_ev.gen) {
        return;  // respuesta de un pedido viejo
    }
    if (r->status != 200 || !r->body) {
        lv_label_set_text_fmt(s_ev.status, r->status ? "La API respondió %d" : "Sin conexión con la API",
                              r->status);
        return;
    }
    cJSON *root = cJSON_ParseWithLength(r->body, r->len);
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
    clear_rows();
    int n = 0;
    const cJSON *ev;
    cJSON_ArrayForEach(ev, items) {
        if (n < MAX_ROWS) {
            add_row(n++, ev);
        }
    }
    cJSON_Delete(root);
    if (n == 0) {
        theme_label(s_ev.list, TH_FONT_BODY, TH_MUTED,
                    s_ev.only_open ? "No hay eventos abiertos." : "No hay eventos.");
    }
    lv_label_set_text_fmt(s_ev.status, "%d evento%s%s", n, n == 1 ? "" : "s",
                          s_ev.only_open ? " abiertos" : "");
    if (s_ev.reload_pending) {
        s_ev.reload_pending = false;
        load();
    }
}

static void load(void)
{
    if (s_ev.loading) {
        s_ev.reload_pending = true;
        return;
    }
    s_ev.loading = true;
    char path[64];
    snprintf(path, sizeof(path), "/events?limit=%d%s", MAX_ROWS, s_ev.only_open ? "&event_state=open" : "");
    lv_label_set_text(s_ev.status, "Cargando…");
    if (api_get(path, LIST_BODY, on_loaded, (void *)(uintptr_t)++s_ev.gen) != ESP_OK) {
        s_ev.loading = false;
    }
}

static void on_filter(lv_event_t *e)
{
    s_ev.only_open = lv_event_get_user_data(e) != NULL;
    lv_obj_set_state(s_ev.btn_open, LV_STATE_CHECKED, s_ev.only_open);
    lv_obj_set_state(s_ev.btn_all, LV_STATE_CHECKED, !s_ev.only_open);
    load();
}

static lv_obj_t *filter_btn(lv_obj_t *parent, const char *text, bool open)
{
    lv_obj_t *b = theme_button_secondary(parent, text);
    lv_obj_set_size(b, 120, 40);
    lv_obj_set_style_bg_color(b, th_color(TH_ACCENT), LV_STATE_CHECKED);
    lv_obj_add_event_cb(b, on_filter, LV_EVENT_CLICKED, open ? (void *)1 : NULL);
    return b;
}

void scr_events_create(lv_obj_t *p)
{
    lv_obj_t *card = lv_obj_create(p);
    theme_card(card);
    lv_obj_set_size(card, SCR_W - 2 * TH_GAP, SCR_H - 2 * TH_GAP);
    lv_obj_set_pos(card, TH_GAP, TH_GAP);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);

    lv_obj_t *bar = lv_obj_create(card);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 8, 0);
    s_ev.btn_open = filter_btn(bar, "Abiertos", true);
    s_ev.btn_all = filter_btn(bar, "Todos", false);
    lv_obj_add_state(s_ev.btn_open, LV_STATE_CHECKED);
    s_ev.status = theme_label(bar, TH_FONT_SMALL, TH_MUTED, "");
    lv_obj_set_style_margin_left(s_ev.status, 8, 0);

    s_ev.list = lv_obj_create(card);
    lv_obj_remove_style_all(s_ev.list);
    lv_obj_set_width(s_ev.list, LV_PCT(100));
    lv_obj_set_flex_grow(s_ev.list, 1);
    lv_obj_set_flex_flow(s_ev.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_ev.list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_ev.list, LV_DIR_VER);
}

void scr_events_refresh(uint32_t dirty)
{
    /* Al entrar (UI_DIRTY_ALL) y cuando llega un evento nuevo por MQTT */
    if (dirty == UI_DIRTY_ALL || (dirty & UI_DIRTY_EVENTS)) {
        load();
    }
}
