/**
 * @file ui_shell.c
 * @brief Implementación de ui_api.h (variante C): barra superior (red, nodos, alarmas, hora),
 *        barra de navegación inferior y cambio de pantalla (Arquitectura de pantalla §3.4).
 */
#include <stdio.h>
#include <string.h>
#include "screens.h"
#include "state_store.h"
#include "theme.h"
#include "ui_api.h"
#include "ui_common.h"
#include "ui_thumbs.h"

typedef enum {
    SCR_HOME, SCR_ENV, SCR_GROW, SCR_ACT, SCR_CAMS, SCR_EVENTS, SCR_CONFIG, SCR_COUNT
} screen_id_t;

static const struct {
    const char *nav;
    const char *title;  /**< NULL = "MasterGrow" */
} SCREENS[SCR_COUNT] = {
    [SCR_HOME] = { "Inicio", NULL },
    [SCR_ENV] = { "Ambiente", "Ambiente" },
    [SCR_GROW] = { "Cultivo", "Cultivo" },
    [SCR_ACT] = { "Actuadores", "Actuadores" },
    [SCR_CAMS] = { "Cámaras", "Cámaras" },
    [SCR_EVENTS] = { "Eventos", "Eventos y alarmas" },
    [SCR_CONFIG] = { "Config", "Configuración" },
};

static struct {
    lv_obj_t *title, *wifi_dot, *status, *alarms, *clock;
    lv_obj_t *content;
    lv_obj_t *pages[SCR_COUNT];
    lv_obj_t *nav[SCR_COUNT];
    screen_id_t current;
} s_sh;

/* ---------------------------------------------------------------- barra superior */

static void topbar_create(lv_obj_t *scr)
{
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 800, TH_TOPBAR_H);
    lv_obj_set_style_bg_color(bar, th_color(TH_BAR), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(bar, 24, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 8, 0);

    s_sh.title = theme_label(bar, TH_FONT_TITLE, TH_FG, "MasterGrow");
    lv_obj_set_flex_grow(s_sh.title, 1);

    theme_label(bar, TH_FONT_BODY, TH_MUTED, "WiFi");
    s_sh.wifi_dot = lv_obj_create(bar);
    lv_obj_remove_style_all(s_sh.wifi_dot);
    lv_obj_set_size(s_sh.wifi_dot, 10, 10);
    lv_obj_set_style_radius(s_sh.wifi_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_sh.wifi_dot, LV_OPA_COVER, 0);

    s_sh.status = theme_label(bar, TH_FONT_BODY, TH_MUTED, "");
    s_sh.alarms = theme_label(bar, TH_FONT_BODY, TH_BAD_BG, "");
    s_sh.clock = theme_label(bar, TH_FONT_BODY, TH_MUTED, "--:--");
}

static void topbar_refresh(void)
{
    ss_conn_t c = state_store_conn();
    uint32_t dot = c.wifi && c.mqtt ? TH_OK_FG : c.wifi ? TH_WARN_BG : TH_BAD_BG;
    lv_obj_set_style_bg_color(s_sh.wifi_dot, th_color(dot), 0);

    static ss_node_t nodes[SS_MAX_NODES];
    int n = state_store_nodes(nodes, SS_MAX_NODES), online = 0;
    for (int i = 0; i < n; i++) {
        online += ui_node_online(&nodes[i]);
    }
    lv_label_set_text_fmt(s_sh.status, "·  Nodos %d/%d  ·", online, n);

    int alarms = ui_active_alarms();
    if (alarms) {
        lv_label_set_text_fmt(s_sh.alarms, "%d alarma%s  ·", alarms, alarms == 1 ? "" : "s");
        lv_obj_remove_flag(s_sh.alarms, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_sh.alarms, LV_OBJ_FLAG_HIDDEN);
    }
    char hhmm[8] = "--:--";
    if (c.time_valid) {
        ui_hhmm(time(NULL), hhmm, sizeof(hhmm));
    }
    lv_label_set_text(s_sh.clock, hhmm);
}

/* ---------------------------------------------------------------- navegación */

static void page_create(screen_id_t id)
{
    lv_obj_t *p = lv_obj_create(s_sh.content);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, SCR_W, SCR_H);
    s_sh.pages[id] = p;
    switch (id) {
    case SCR_HOME: scr_home_create(p); break;
    case SCR_CAMS: scr_cameras_create(p); break;
    case SCR_EVENTS: scr_events_create(p); break;
    case SCR_ENV:
        scr_placeholder_create(p, "Ambiente", "Series de temperatura, humedad, CO2 y luz: llegan cuando el "
                               "módulo de control publique su estado (Contrato §4.4).");
        break;
    case SCR_GROW:
        scr_placeholder_create(p, "Cultivo", "Variables según el modo de cultivo de la zona: esperan al "
                               "módulo de control.");
        break;
    case SCR_ACT:
        scr_placeholder_create(p, "Actuadores", "Relés del módulo de control (bomba, aire, luz, "
                               "ventilación): esperan al módulo de control.");
        break;
    case SCR_CONFIG:
        scr_placeholder_create(p, "Configuración", "Red, ciclo de luz, captura global, umbrales y alta "
                               "de nodos: llega en D4.");
        break;
    default: break;
    }
}

static void refresh_current(uint32_t dirty)
{
    switch (s_sh.current) {
    case SCR_HOME: scr_home_refresh(dirty); break;
    case SCR_CAMS: scr_cameras_refresh(dirty); break;
    case SCR_EVENTS: scr_events_refresh(dirty); break;
    default: break;
    }
}

static void show(screen_id_t id)
{
    if (!s_sh.pages[id]) {
        page_create(id);
    }
    for (int i = 0; i < SCR_COUNT; i++) {
        if (s_sh.pages[i]) {
            lv_obj_set_flag(s_sh.pages[i], LV_OBJ_FLAG_HIDDEN, i != (int)id);
        }
        lv_obj_set_state(s_sh.nav[i], LV_STATE_CHECKED, i == (int)id);
    }
    s_sh.current = id;
    lv_label_set_text(s_sh.title, SCREENS[id].title ? SCREENS[id].title : "MasterGrow");
    refresh_current(UI_DIRTY_ALL);
}

static void on_nav(lv_event_t *e)
{
    show((screen_id_t)(intptr_t)lv_event_get_user_data(e));
}

static void navbar_create(lv_obj_t *scr)
{
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 800, TH_NAVBAR_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, th_color(TH_BAR), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(bar, 12, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < SCR_COUNT; i++) {
        lv_obj_t *b = lv_button_create(bar);
        lv_obj_set_size(b, 96, 52);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(b, th_color(TH_ACCENT), LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(b, LV_OPA_40, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(b, th_color(TH_ACCENT), LV_STATE_PRESSED);
        lv_obj_t *l = theme_label(b, TH_FONT_BODY, TH_MUTED, SCREENS[i].nav);
        lv_obj_set_style_text_color(l, th_color(TH_FG), LV_STATE_CHECKED);
        lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, on_nav, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_sh.nav[i] = b;
    }
    /* Voz (opcional, Arquitectura §5): sin micrófono en la placa, queda deshabilitado */
    lv_obj_t *mic = lv_button_create(bar);
    lv_obj_set_size(mic, 64, 52);
    lv_obj_set_style_radius(mic, 8, 0);
    lv_obj_set_style_shadow_width(mic, 0, 0);
    lv_obj_set_style_bg_color(mic, th_color(TH_BAD_BG), 0);
    lv_obj_set_style_bg_opa(mic, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_add_state(mic, LV_STATE_DISABLED);
    lv_obj_t *ml = lv_label_create(mic);
    lv_label_set_text(ml, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(ml, &lv_font_montserrat_20, 0);
    lv_obj_center(ml);
}

/* ---------------------------------------------------------------- ui_api */

void ui_init(void)
{
    theme_init();
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    topbar_create(scr);
    s_sh.content = lv_obj_create(scr);
    lv_obj_remove_style_all(s_sh.content);
    lv_obj_set_size(s_sh.content, SCR_W, SCR_H);
    lv_obj_set_pos(s_sh.content, 0, TH_TOPBAR_H);
    navbar_create(scr);

    topbar_refresh();
    show(SCR_HOME);
}

void ui_refresh(uint32_t dirty)
{
    if (dirty & UI_DIRTY_THUMB) {
        ui_thumbs_sync();
    }
    if (dirty & (UI_DIRTY_CONN | UI_DIRTY_NODES | UI_DIRTY_EVENTS | UI_DIRTY_TICK)) {
        topbar_refresh();
    }
    refresh_current(dirty);
}
