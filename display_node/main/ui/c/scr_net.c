/**
 * @file scr_net.c
 * @brief Red y servidor (se abre desde Config): WiFi (con búsqueda de redes), servidor (IP o
 *        nombre .local), contraseña MQTT y token de la API, con teclado en pantalla. Guardar
 *        escribe NVS (mg_settings, en net_setup) y reinicia la pantalla.
 */
#include <stdio.h>
#include <string.h>
#include "net_setup.h"
#include "screens.h"
#include "theme.h"
#include "ui_api.h"

#define FORM_W   488
#define KB_H     216

typedef enum { F_SSID, F_WPASS, F_SERVER, F_MPASS, F_TOKEN, F_COUNT } field_t;

static const char *LABELS[F_COUNT] = { "Red WiFi", "Contraseña WiFi", "Servidor (IP o nombre .local)",
                                       "Contraseña MQTT (usuario " CONFIG_MG_NODE_ID ")", "Token de la API" };

static struct {
    lv_obj_t *form, *ta[F_COUNT], *scan_btn, *aps, *status, *kb, *save, *reset;
    lv_timer_t *timer;
    mg_wifi_ap_t found[NET_SETUP_MAX_APS];
    int n_found;
    void (*on_back)(void);
} s_n;

/* ---------------------------------------------------------------- teclado */

static void kb_hide(void)
{
    lv_obj_add_flag(s_n.kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_n.kb, NULL);
    lv_obj_set_height(s_n.form, SCR_H - 2 * TH_GAP);
}

static void on_ta(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    if (lv_event_get_code(e) == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(s_n.kb, ta);
        lv_obj_remove_flag(s_n.kb, LV_OBJ_FLAG_HIDDEN);
        /* El formulario termina arriba del teclado y el campo queda a la vista */
        lv_obj_set_height(s_n.form, 480 - TH_TOPBAR_H - KB_H - TH_GAP);
        lv_obj_update_layout(s_n.form);
        lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
    } else if (lv_event_get_code(e) == LV_EVENT_DEFOCUSED) {
        kb_hide();
    }
}

static void on_kb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_READY || c == LV_EVENT_CANCEL) {
        lv_obj_t *ta = lv_keyboard_get_textarea(s_n.kb);
        if (ta) {
            lv_obj_remove_state(ta, LV_STATE_FOCUSED);
        }
        kb_hide();
    }
}

/* ---------------------------------------------------------------- búsqueda de redes */

static void on_scan(lv_event_t *e)
{
    net_setup_scan();
    theme_button_set(s_n.scan_btn, "Buscando…", false);
}

static void on_ap(lv_event_t *e)
{
    int i = (int)lv_dropdown_get_selected(s_n.aps) - 1;  // 0 = "Elegí una red"
    if (i >= 0 && i < s_n.n_found) {
        lv_textarea_set_text(s_n.ta[F_SSID], s_n.found[i].ssid);
        lv_obj_add_state(s_n.ta[F_WPASS], LV_STATE_FOCUSED);  // siguiente: la contraseña
        lv_obj_send_event(s_n.ta[F_WPASS], LV_EVENT_FOCUSED, NULL);
    }
}

static void poll(lv_timer_t *t)
{
    int n = net_setup_scan_result(s_n.found, NET_SETUP_MAX_APS);
    if (n < 0) {
        return;  // buscando o nada pedido
    }
    s_n.n_found = n;
    theme_button_set(s_n.scan_btn, "Buscar", true);
    char opts[NET_SETUP_MAX_APS * 48] = "Elegí una red";
    size_t len = strlen(opts);
    for (int i = 0; i < n; i++) {
        len += snprintf(opts + len, sizeof(opts) - len, "\n%s  (%d dBm%s)", s_n.found[i].ssid, s_n.found[i].rssi,
                        s_n.found[i].secured ? "" : ", abierta");
    }
    lv_dropdown_set_options(s_n.aps, opts);
    lv_obj_remove_flag(s_n.aps, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(s_n.status, n ? "%d redes encontradas" : "No se encontraron redes", n);
}

/* ---------------------------------------------------------------- acciones */

static void on_save(lv_event_t *e)
{
    mg_settings_t s;
    net_setup_current(&s);
    strlcpy(s.wifi_ssid, lv_textarea_get_text(s_n.ta[F_SSID]), sizeof(s.wifi_ssid));
    strlcpy(s.wifi_pass, lv_textarea_get_text(s_n.ta[F_WPASS]), sizeof(s.wifi_pass));
    strlcpy(s.server, lv_textarea_get_text(s_n.ta[F_SERVER]), sizeof(s.server));
    strlcpy(s.mqtt_pass, lv_textarea_get_text(s_n.ta[F_MPASS]), sizeof(s.mqtt_pass));
    strlcpy(s.api_token, lv_textarea_get_text(s_n.ta[F_TOKEN]), sizeof(s.api_token));
    if (!s.wifi_ssid[0] || !s.server[0]) {
        lv_label_set_text(s_n.status, "Falta la red WiFi o el servidor");
        return;
    }
    lv_label_set_text(s_n.status, "Guardando… la pantalla se reinicia y se conecta con los datos nuevos");
    theme_button_set(s_n.save, "Reiniciando…", false);
    net_setup_save_and_restart(&s);
}

static void on_reset(lv_event_t *e)
{
    lv_label_set_text(s_n.status, "Volviendo a los valores de fábrica… se reinicia");
    theme_button_set(s_n.reset, "Reiniciando…", false);
    net_setup_reset_and_restart();
}

static void on_back(lv_event_t *e)
{
    if (s_n.on_back) {
        s_n.on_back();
    }
}

/* ---------------------------------------------------------------- armado */

static lv_obj_t *field(lv_obj_t *parent, field_t f, bool secret)
{
    theme_label(parent, TH_FONT_SMALL, TH_MUTED, LABELS[f]);
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, secret);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_set_style_bg_color(ta, th_color(TH_CARD_ALT), 0);
    lv_obj_set_style_border_width(ta, 0, 0);
    lv_obj_set_style_border_width(ta, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(ta, th_color(TH_OK_FG), LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(ta, th_color(TH_FG), 0);
    lv_obj_set_style_text_font(ta, theme_font(TH_FONT_BODY), 0);
    lv_obj_add_event_cb(ta, on_ta, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta, on_ta, LV_EVENT_DEFOCUSED, NULL);
    s_n.ta[f] = ta;
    return ta;
}

void scr_net_create(lv_obj_t *p, void (*on_back_cb)(void))
{
    s_n.on_back = on_back_cb;

    s_n.form = lv_obj_create(p);
    theme_card(s_n.form);
    lv_obj_set_size(s_n.form, FORM_W, SCR_H - 2 * TH_GAP);
    lv_obj_set_pos(s_n.form, TH_GAP, TH_GAP);
    lv_obj_add_flag(s_n.form, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_n.form, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_n.form, 6, 0);

    lv_obj_t *row = lv_obj_create(s_n.form);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_t *col = lv_obj_create(row);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 6, 0);
    field(col, F_SSID, false);
    s_n.scan_btn = theme_button_secondary(row, "Buscar");
    lv_obj_set_width(s_n.scan_btn, 104);
    lv_obj_add_event_cb(s_n.scan_btn, on_scan, LV_EVENT_CLICKED, NULL);
    s_n.aps = theme_dropdown(s_n.form, "Elegí una red");
    lv_obj_set_width(s_n.aps, LV_PCT(100));
    lv_obj_add_flag(s_n.aps, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_n.aps, on_ap, LV_EVENT_VALUE_CHANGED, NULL);

    field(s_n.form, F_WPASS, true);
    field(s_n.form, F_SERVER, false);
    field(s_n.form, F_MPASS, true);
    field(s_n.form, F_TOKEN, true);

    lv_obj_t *side = lv_obj_create(p);
    theme_card(side);
    lv_obj_set_size(side, SCR_W - FORM_W - 3 * TH_GAP, SCR_H - 2 * TH_GAP);
    lv_obj_set_pos(side, FORM_W + 2 * TH_GAP, TH_GAP);
    lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(side, 10, 0);
    lv_obj_t *help = theme_label(side, TH_FONT_SMALL, TH_MUTED,
                                 "Se guardan en esta pantalla (NVS). El servidor puede ser una IP o un "
                                 "nombre .local: así no depende de la IP de la computadora.");
    lv_obj_set_width(help, LV_PCT(100));
    lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);
    s_n.status = theme_label(side, TH_FONT_SMALL, TH_FG, "");
    lv_obj_set_width(s_n.status, LV_PCT(100));
    lv_label_set_long_mode(s_n.status, LV_LABEL_LONG_WRAP);
    lv_obj_t *spacer = lv_obj_create(side);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_flex_grow(spacer, 1);
    s_n.save = theme_button(side, "Guardar y reiniciar");
    lv_obj_set_width(s_n.save, LV_PCT(100));
    lv_obj_add_event_cb(s_n.save, on_save, LV_EVENT_CLICKED, NULL);
    s_n.reset = theme_button_secondary(side, "Valores de fábrica");
    lv_obj_set_width(s_n.reset, LV_PCT(100));
    lv_obj_add_event_cb(s_n.reset, on_reset, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back = theme_button_secondary(side, LV_SYMBOL_LEFT " Volver");
    lv_obj_set_width(back, LV_PCT(100));
    lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, NULL);

    /* Teclado sobre todo (capa superior), abajo; aparece al tocar un campo */
    s_n.kb = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(s_n.kb, 800, KB_H);
    lv_obj_align(s_n.kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(s_n.kb, theme_font(TH_FONT_TITLE), LV_PART_ITEMS);
    lv_obj_add_event_cb(s_n.kb, on_kb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(s_n.kb, LV_OBJ_FLAG_HIDDEN);
}

void scr_net_enter(void)
{
    mg_settings_t s;
    net_setup_current(&s);
    lv_textarea_set_text(s_n.ta[F_SSID], s.wifi_ssid);
    lv_textarea_set_text(s_n.ta[F_WPASS], s.wifi_pass);
    lv_textarea_set_text(s_n.ta[F_SERVER], s.server);
    lv_textarea_set_text(s_n.ta[F_MPASS], s.mqtt_pass);
    lv_textarea_set_text(s_n.ta[F_TOKEN], s.api_token);
    lv_label_set_text(s_n.status, "");
    if (!s_n.timer) {
        s_n.timer = lv_timer_create(poll, 200, NULL);
    }
}

void scr_net_leave(void)
{
    kb_hide();
    if (s_n.timer) {
        lv_timer_delete(s_n.timer);
        s_n.timer = NULL;
    }
}
