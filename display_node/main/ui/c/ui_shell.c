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
#include "api_jobs.h"
#include "cJSON.h"

typedef enum {
    SCR_HOME, SCR_ENV, SCR_GROW, SCR_ACT, SCR_CAMS, SCR_EVENTS, SCR_CONFIG, SCR_COUNT,
    SCR_LIVE = SCR_COUNT,  /**< no están en la barra: se abren desde una cámara */
    SCR_DETAIL,
    SCR_NET,
    SCR_ALL
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
    lv_obj_t *pages[SCR_ALL];
    lv_obj_t *nav[SCR_COUNT];
    screen_id_t current;
    screen_id_t before_live;    /**< pantalla de la barra desde la que se abrió live/detalle */
    char live_node[16];
    int open_alarms;            /**< eventos abiertos según la API (-1 = todavía no se sabe) */
    bool alarms_loading;
    uint32_t alarm_tick;
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

    /* Eventos abiertos según la API; mientras no responde, estimación con lo recibido por MQTT */
    int alarms = s_sh.open_alarms >= 0 ? s_sh.open_alarms : ui_active_alarms();
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

static void live_back(void);

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
    case SCR_ENV: scr_env_create(p); break;
    case SCR_GROW: scr_grow_create(p); break;
    case SCR_ACT: scr_act_create(p); break;
    case SCR_LIVE:
        scr_live_create(p, live_back);
        break;
    case SCR_DETAIL:
        scr_camera_create(p, live_back);
        break;
    case SCR_NET:
        scr_net_create(p, live_back);
        break;
    case SCR_CONFIG:
        scr_config_create(p);
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
    case SCR_ENV: scr_env_refresh(dirty); break;
    case SCR_GROW: scr_grow_refresh(dirty); break;
    case SCR_ACT: scr_act_refresh(dirty); break;
    case SCR_DETAIL: scr_camera_refresh(dirty); break;
    case SCR_CONFIG: scr_config_refresh(dirty); break;
    default: break;
    }
}

static void show(screen_id_t id)
{
    if (s_sh.current == SCR_LIVE && id != SCR_LIVE) {
        scr_live_leave();  // salir de la vista en vivo cierra la sesión (SRS F-05)
    }
    if (s_sh.current == SCR_NET && id != SCR_NET) {
        scr_net_leave();
    }
    if (!s_sh.pages[id]) {
        page_create(id);
    }
    for (int i = 0; i < SCR_ALL; i++) {
        if (s_sh.pages[i]) {
            lv_obj_set_flag(s_sh.pages[i], LV_OBJ_FLAG_HIDDEN, i != (int)id);
        }
        if (i < SCR_COUNT) {
            /* En live/detalle queda marcada la pantalla desde la que se abrieron */
            screen_id_t marked = id >= SCR_COUNT ? s_sh.before_live : id;
            lv_obj_set_state(s_sh.nav[i], LV_STATE_CHECKED, i == (int)marked);
        }
    }
    s_sh.current = id;
    if (id == SCR_LIVE) {
        lv_label_set_text_fmt(s_sh.title, "En vivo · %s", s_sh.live_node);
    } else if (id == SCR_DETAIL) {
        lv_label_set_text_fmt(s_sh.title, "Cámara · %s", s_sh.live_node);
    } else if (id == SCR_NET) {
        lv_label_set_text(s_sh.title, "Red y servidor");
    } else {
        lv_label_set_text(s_sh.title, SCREENS[id].title ? SCREENS[id].title : "MasterGrow");
    }
    refresh_current(UI_DIRTY_ALL);
}

static void live_back(void)
{
    show(s_sh.before_live);
}

void ui_shell_open_net(void)
{
    if (s_sh.current < SCR_COUNT) {
        s_sh.before_live = s_sh.current;
    }
    show(SCR_NET);
    scr_net_enter();
}

void ui_shell_open_camera(const char *node_id)
{
    if (s_sh.current < SCR_COUNT) {
        s_sh.before_live = s_sh.current;
    }
    strlcpy(s_sh.live_node, node_id, sizeof(s_sh.live_node));
    show(SCR_DETAIL);
    scr_camera_enter(node_id);
}

void ui_shell_open_live(const char *node_id)
{
    if (s_sh.current < SCR_COUNT) {
        s_sh.before_live = s_sh.current;
    }
    strlcpy(s_sh.live_node, node_id, sizeof(s_sh.live_node));
    show(SCR_LIVE);
    scr_live_enter(node_id);
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

/* ---------------------------------------------------------------- alarmas (API) */

static void on_alarms(api_result_t *r, void *ctx)
{
    s_sh.alarms_loading = false;
    if (r->status != 200 || !r->body) {
        return;  // queda el último valor conocido
    }
    cJSON *root = cJSON_ParseWithLength(r->body, r->len);
    s_sh.open_alarms = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(root, "items"));
    cJSON_Delete(root);
    topbar_refresh();
}

static void alarms_poll(void)
{
    if (s_sh.alarms_loading || !state_store_conn().wifi) {
        return;
    }
    s_sh.alarms_loading = true;
    if (api_get("/events?event_state=open&limit=100", 48 * 1024, on_alarms, NULL) != ESP_OK) {
        s_sh.alarms_loading = false;
    }
}

void ui_shell_alarms_changed(void)
{
    alarms_poll();
}

/* ---------------------------------------------------------------- ui_api */

void ui_init(void)
{
    s_sh.open_alarms = -1;
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
    /* Alarmas: al llegar un evento o cambiar un nodo (la API abre/cierra node_disconnected) y cada 60 s */
    if (dirty & (UI_DIRTY_EVENTS | UI_DIRTY_NODES)) {
        alarms_poll();
    } else if ((dirty & UI_DIRTY_TICK) && ++s_sh.alarm_tick >= 60) {
        s_sh.alarm_tick = 0;
        alarms_poll();
    }
    if (dirty & UI_DIRTY_THUMB) {
        ui_thumbs_sync();
    }
    if (dirty & (UI_DIRTY_CONN | UI_DIRTY_NODES | UI_DIRTY_EVENTS | UI_DIRTY_TICK)) {
        topbar_refresh();
    }
    refresh_current(dirty);
}
