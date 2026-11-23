/**
 * @file scr_config.c
 * @brief Configuración (SRS de pantalla F-07): parámetros globales de captura de todas las cámaras
 *        (GET/PUT /config/global) y datos de este equipo (red, API, memoria). La ventana de luz
 *        global la pisa la de cada zona si la tiene (Contrato §7).
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "api_jobs.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "mg_api.h"
#include "mg_wifi.h"
#include "screens.h"
#include "state_store.h"
#include "sdkconfig.h"
#include "theme.h"
#include "ui_api.h"
#include "ui_common.h"

static const int INTERVALS[] = { 1, 5, 10, 15, 30, 60, 120, 240, 480 };
static const char *INTERVAL_OPTS = "1 min\n5 min\n10 min\n15 min\n30 min\n1 h\n2 h\n4 h\n8 h";
static const char *MODES[] = { "hsv", "hybrid", "model" };
static const char *MODE_OPTS = "Solo color (HSV)\nHíbrido\nModelo";
static const int THRESH[] = { 15, 20, 25, 30, 35, 40, 45, 50, 60 };  /* centésimos */
static const char *THRESH_OPTS = "0,15\n0,20\n0,25\n0,30\n0,35\n0,40\n0,45\n0,50\n0,60";

typedef enum { G_INTERVAL, G_START, G_END, G_THRESH, G_MODE, G_COUNT } gparam_t;

#define FW_KINDS 3
static const char *FW_KIND[FW_KINDS] = { "cam", "disp", "ctrl" };

static struct {
    lv_obj_t *fw_list;
    char fw_id[FW_KINDS][16], fw_ver[FW_KINDS][32];   /**< último firmware subido por tipo */
    char updating[SS_MAX_NODES][SS_ID_LEN];           /**< nodos con ota_update en curso */
    char updating_to[SS_MAX_NODES][32];
    lv_obj_t *dd[G_COUNT], *src[G_COUNT], *status, *sys;
    int loaded[G_COUNT];
    uint32_t gen;
    char hours[48 * 6 + 1];   /**< "00:00\n00:30\n..." */
} s_cf;

static int nearest(const int *arr, int n, int v)
{
    int best = 0;
    for (int i = 0; i < n; i++) {
        if (abs(arr[i] - v) < abs(arr[best] - v)) {
            best = i;
        }
    }
    return best;
}

/** "06:00" → índice de media hora (0–47). */
static int hhmm_index(const char *s)
{
    int h = 0, m = 0;
    if (s && sscanf(s, "%d:%d", &h, &m) == 2) {
        return ((h % 24) * 60 + m + 15) / 30 % 48;
    }
    return 0;
}

static void index_hhmm(int i, char *buf, size_t len)
{
    unsigned u = (unsigned)i % 48;
    snprintf(buf, len, "%02u:%02u", u / 2, (u % 2) * 30);
}

static const char *source_text(const char *s)
{
    return s && strcmp(s, "global") == 0 ? "cambiado" : "por defecto";
}

static void on_loaded(api_result_t *r, void *ctx)
{
    if ((uint32_t)(uintptr_t)ctx != s_cf.gen) {
        return;
    }
    if (r->status != 200 || !r->body) {
        lv_label_set_text_fmt(s_cf.status, r->status ? "La API respondió %d" : "Sin conexión con la API",
                              r->status);
        return;
    }
    cJSON *root = cJSON_ParseWithLength(r->body, r->len);
    const cJSON *cfg = cJSON_GetObjectItemCaseSensitive(root, "config");
    const cJSON *src = cJSON_GetObjectItemCaseSensitive(root, "sources");
    const cJSON *lw = cJSON_GetObjectItemCaseSensitive(cfg, "light_window");
    s_cf.loaded[G_INTERVAL] = nearest(INTERVALS, 9,
        (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(cfg, "capture_interval_min")));
    s_cf.loaded[G_START] = hhmm_index(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(lw, "start")));
    s_cf.loaded[G_END] = hhmm_index(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(lw, "end")));
    s_cf.loaded[G_THRESH] = nearest(THRESH, 9,
        (int)(cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(cfg, "hsv_suspicion_threshold")) * 100 + 0.5));
    const char *mode = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(cfg, "analysis_mode"));
    s_cf.loaded[G_MODE] = 0;
    for (int i = 0; mode && i < 3; i++) {
        if (strcmp(MODES[i], mode) == 0) {
            s_cf.loaded[G_MODE] = i;
        }
    }
    const char *keys[G_COUNT] = { "capture_interval_min", "light_window", "light_window",
                                  "hsv_suspicion_threshold", "analysis_mode" };
    for (int p = 0; p < G_COUNT; p++) {
        lv_dropdown_set_selected(s_cf.dd[p], s_cf.loaded[p]);
        lv_label_set_text(s_cf.src[p], source_text(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(src, keys[p]))));
    }
    const cJSON *ver = cJSON_GetObjectItemCaseSensitive(root, "version");
    lv_label_set_text_fmt(s_cf.status, "Configuración global v%d", cJSON_IsNumber(ver) ? ver->valueint : 0);
    cJSON_Delete(root);
}

static void load(void)
{
    lv_label_set_text(s_cf.status, "Cargando…");
    api_get("/config/global", 4096, on_loaded, (void *)(uintptr_t)++s_cf.gen);
}

static void on_saved(api_result_t *r, void *ctx)
{
    if (r->status == 200) {
        load();
        lv_label_set_text(s_cf.status, "Guardado: las cámaras la aplican en segundos");
    } else {
        lv_label_set_text_fmt(s_cf.status, "No se pudo guardar (HTTP %d)", r->status);
    }
}

static void on_save(lv_event_t *e)
{
    int sel[G_COUNT];
    for (int p = 0; p < G_COUNT; p++) {
        sel[p] = (int)lv_dropdown_get_selected(s_cf.dd[p]);
    }
    cJSON *o = cJSON_CreateObject();
    if (sel[G_INTERVAL] != s_cf.loaded[G_INTERVAL]) {
        cJSON_AddNumberToObject(o, "capture_interval_min", INTERVALS[sel[G_INTERVAL]]);
    }
    if (sel[G_START] != s_cf.loaded[G_START] || sel[G_END] != s_cf.loaded[G_END]) {
        char a[12], b[12];
        index_hhmm(sel[G_START], a, sizeof(a));
        index_hhmm(sel[G_END], b, sizeof(b));
        cJSON *lw = cJSON_AddObjectToObject(o, "light_window");
        cJSON_AddStringToObject(lw, "start", a);
        cJSON_AddStringToObject(lw, "end", b);
    }
    if (sel[G_THRESH] != s_cf.loaded[G_THRESH]) {
        cJSON_AddNumberToObject(o, "hsv_suspicion_threshold", THRESH[sel[G_THRESH]] / 100.0);
    }
    if (sel[G_MODE] != s_cf.loaded[G_MODE]) {
        cJSON_AddStringToObject(o, "analysis_mode", MODES[sel[G_MODE]]);
    }
    if (!cJSON_GetArraySize(o)) {
        lv_label_set_text(s_cf.status, "No hay cambios para guardar");
        cJSON_Delete(o);
        return;
    }
    char *json = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    lv_label_set_text(s_cf.status, "Guardando…");
    api_send("PUT", "/config/global", json, on_saved, NULL);
    cJSON_free(json);
}

static lv_obj_t *param_row(lv_obj_t *parent, const char *label, const char *opts, gparam_t p)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 10, 0);
    lv_obj_t *l = theme_label(r, TH_FONT_BODY, TH_FG, label);
    lv_obj_set_width(l, 150);
    s_cf.dd[p] = theme_dropdown(r, opts);
    lv_obj_set_width(s_cf.dd[p], 150);
    s_cf.src[p] = theme_label(r, TH_FONT_SMALL, TH_MUTED, "");
    return r;
}

static void refresh_sys(void)
{
    char ip[16] = "-";
    mg_wifi_get_ip_str(ip, sizeof(ip));
    int64_t up = esp_timer_get_time() / 1000000;
    lv_label_set_text_fmt(s_cf.sys,
                          "Nodo  %s · fw %s\n"
                          "IP  %s · WiFi %d dBm\n"
                          "Broker  %s\n"
                          "API  %s\n"
                          "Memoria  interna %u KB · PSRAM %u KB\n"
                          "Encendido hace %d h %02d min",
                          CONFIG_MG_NODE_ID, esp_app_get_description()->version, ip,
                          mg_wifi_is_connected() ? mg_wifi_get_rssi() : 0, CONFIG_MG_MQTT_BROKER_URI,
                          mg_api_base_url(), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024), (int)(up / 3600),
                          (int)(up / 60 % 60));
}

/* ---------------------------------------------------------------- firmware (OTA) */

static int kind_index(mg_node_kind_t k)
{
    return k == MG_NODE_CAM ? 0 : k == MG_NODE_DISP ? 1 : 2;
}

static int updating_slot(const char *node, bool create)
{
    for (int i = 0; i < SS_MAX_NODES; i++) {
        if (strcmp(s_cf.updating[i], node) == 0) {
            return i;
        }
    }
    for (int i = 0; create && i < SS_MAX_NODES; i++) {
        if (!s_cf.updating[i][0]) {
            strlcpy(s_cf.updating[i], node, SS_ID_LEN);
            return i;
        }
    }
    return -1;
}

static void fw_render(void);

static void on_deployed(api_result_t *r, void *ctx)
{
    int slot = (int)(intptr_t)ctx;
    if (r->status != 200 || !r->body || !strstr(r->body, "\"sent\":true")) {
        lv_label_set_text_fmt(s_cf.status, "No se pudo enviar la actualización (HTTP %d)", r->status);
        s_cf.updating[slot][0] = '\0';
    }
    fw_render();
}

static void on_update(lv_event_t *e)
{
    static ss_node_t n;
    const char *node = lv_obj_get_user_data(lv_event_get_target(e));
    if (!state_store_node(node, &n)) {
        return;
    }
    int k = kind_index(n.kind);
    int slot = updating_slot(node, true);
    if (slot < 0 || !s_cf.fw_id[k][0]) {
        return;
    }
    strlcpy(s_cf.updating_to[slot], s_cf.fw_ver[k], sizeof(s_cf.updating_to[slot]));
    char path[48], body[48];
    snprintf(path, sizeof(path), "/firmware/%s/deploy", s_cf.fw_id[k]);
    snprintf(body, sizeof(body), "{\"node_ids\":[\"%s\"]}", node);
    api_send("POST", path, body, on_deployed, (void *)(intptr_t)slot);
    fw_render();
}

/** Lista de nodos con su firmware y, si hay uno más nuevo subido, el botón para actualizar. */
static void fw_render(void)
{
    static ss_node_t nodes[SS_MAX_NODES];
    static char ids[SS_MAX_NODES][SS_ID_LEN];  /* user_data de los botones: vive mientras existan */
    int n = state_store_nodes(nodes, SS_MAX_NODES);
    lv_obj_clean(s_cf.fw_list);
    for (int i = 0; i < n; i++) {
        const ss_node_t *nd = &nodes[i];
        int k = kind_index(nd->kind);
        int slot = updating_slot(nd->node_id, false);
        if (slot >= 0 && strcmp(nd->firmware, s_cf.updating_to[slot]) == 0) {
            s_cf.updating[slot][0] = '\0';  // ya informa la versión nueva
            slot = -1;
        }
        lv_obj_t *r = lv_obj_create(s_cf.fw_list);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(r, 6, 0);
        lv_obj_t *l = theme_label(r, TH_FONT_SMALL, TH_FG, "");
        lv_label_set_text_fmt(l, "%s  %s", nd->node_id, nd->firmware[0] ? nd->firmware : "-");
        lv_obj_set_flex_grow(l, 1);
        if (slot >= 0) {
            theme_label(r, TH_FONT_SMALL, TH_WARN_BG, "actualizando…");
        } else if (s_cf.fw_ver[k][0] && strcmp(s_cf.fw_ver[k], nd->firmware) != 0 && ui_node_online(nd)) {
            strlcpy(ids[i], nd->node_id, SS_ID_LEN);
            char txt[48];
            snprintf(txt, sizeof(txt), "A %s", s_cf.fw_ver[k]);
            lv_obj_t *b = theme_button(r, txt);
            lv_obj_set_size(b, LV_SIZE_CONTENT, 34);
            lv_obj_set_style_pad_hor(b, 10, 0);
            lv_obj_set_user_data(b, ids[i]);
            lv_obj_add_event_cb(b, on_update, LV_EVENT_CLICKED, NULL);
        } else {
            theme_label(r, TH_FONT_SMALL, TH_MUTED, "al día");
        }
    }
}

static void on_fw_list(api_result_t *r, void *ctx)
{
    if (r->status != 200 || !r->body) {
        return;
    }
    memset(s_cf.fw_id, 0, sizeof(s_cf.fw_id));
    memset(s_cf.fw_ver, 0, sizeof(s_cf.fw_ver));
    cJSON *root = cJSON_ParseWithLength(r->body, r->len);
    const cJSON *it;
    /* La lista viene del más nuevo al más viejo: el primero de cada tipo es el último subido */
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "items")) {
        const char *kind = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(it, "kind"));
        for (int k = 0; kind && k < FW_KINDS; k++) {
            if (strcmp(kind, FW_KIND[k]) == 0 && !s_cf.fw_id[k][0]) {
                strlcpy(s_cf.fw_id[k], cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(it, "firmware_id")),
                        sizeof(s_cf.fw_id[k]));
                strlcpy(s_cf.fw_ver[k], cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(it, "version")),
                        sizeof(s_cf.fw_ver[k]));
            }
        }
    }
    cJSON_Delete(root);
    fw_render();
}

void scr_config_create(lv_obj_t *p)
{
    int len = 0;
    for (int i = 0; i < 48; i++) {
        len += snprintf(s_cf.hours + len, sizeof(s_cf.hours) - len, "%s%02d:%02d", i ? "\n" : "", i / 2,
                        (i % 2) * 30);
    }

    lv_obj_t *cap = lv_obj_create(p);
    theme_card(cap);
    lv_obj_set_size(cap, 468, SCR_H - 2 * TH_GAP);
    lv_obj_set_pos(cap, TH_GAP, TH_GAP);
    lv_obj_set_flex_flow(cap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cap, 6, 0);
    lv_obj_add_flag(cap, LV_OBJ_FLAG_SCROLLABLE);
    theme_label(cap, TH_FONT_SMALL, TH_MUTED, "CAPTURA · TODAS LAS CÁMARAS");
    param_row(cap, "Intervalo", INTERVAL_OPTS, G_INTERVAL);
    param_row(cap, "Luz desde", s_cf.hours, G_START);
    param_row(cap, "Luz hasta", s_cf.hours, G_END);
    param_row(cap, "Umbral HSV", THRESH_OPTS, G_THRESH);
    param_row(cap, "Análisis", MODE_OPTS, G_MODE);
    lv_obj_t *r = lv_obj_create(cap);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 10, 0);
    lv_obj_t *save = theme_button(r, "Guardar");
    lv_obj_set_width(save, 140);
    lv_obj_add_event_cb(save, on_save, LV_EVENT_CLICKED, NULL);
    s_cf.status = theme_label(r, TH_FONT_SMALL, TH_MUTED, "");
    lv_obj_set_flex_grow(s_cf.status, 1);
    lv_label_set_long_mode(s_cf.status, LV_LABEL_LONG_WRAP);

    lv_obj_t *sys = lv_obj_create(p);
    theme_card(sys);
    lv_obj_set_size(sys, SCR_W - 468 - 3 * TH_GAP, SCR_H - 2 * TH_GAP);
    lv_obj_set_pos(sys, 468 + 2 * TH_GAP, TH_GAP);
    lv_obj_set_flex_flow(sys, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sys, 8, 0);
    theme_label(sys, TH_FONT_SMALL, TH_MUTED, "ESTE EQUIPO");
    s_cf.sys = theme_label(sys, TH_FONT_SMALL, TH_FG, "");
    lv_obj_set_width(s_cf.sys, LV_PCT(100));
    lv_label_set_long_mode(s_cf.sys, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(s_cf.sys, 4, 0);
    lv_obj_add_flag(sys, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *fwt = theme_label(sys, TH_FONT_SMALL, TH_MUTED, "FIRMWARE (OTA)");
    lv_obj_set_style_margin_top(fwt, 6, 0);
    s_cf.fw_list = lv_obj_create(sys);
    lv_obj_remove_style_all(s_cf.fw_list);
    lv_obj_set_size(s_cf.fw_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_cf.fw_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_cf.fw_list, 4, 0);
}

void scr_config_refresh(uint32_t dirty)
{
    if (dirty == UI_DIRTY_ALL) {
        load();
        api_get("/firmware", 16 * 1024, on_fw_list, NULL);
    } else if (dirty & UI_DIRTY_NODES) {
        fw_render();  // un nodo informó otra versión (p. ej. terminó una actualización)
    }
    if (dirty & (UI_DIRTY_TICK | UI_DIRTY_CONN)) {
        refresh_sys();
    }
}
