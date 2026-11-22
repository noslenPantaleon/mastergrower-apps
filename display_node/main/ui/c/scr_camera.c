/**
 * @file scr_camera.c
 * @brief Detalle de cámara (SRS de pantalla F-04): última imagen e historial (API), tipo de disparo,
 *        análisis local, estado del nodo y edición del override (Contrato §7): intervalo,
 *        resoluciones, modo de análisis y calidad JPEG. Se abre tocando una tarjeta de Cámaras.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "api_jobs.h"
#include "cJSON.h"
#include "command_publisher.h"
#include "esp_heap_caps.h"
#include "screens.h"
#include "state_store.h"
#include "theme.h"
#include "ui_api.h"
#include "ui_common.h"
#include "ui_thumbs.h"

#define HIST_N     12
#define IMG_W      320
#define IMG_H      240

/* ---------------------------------------------------------------- opciones del override */

static const int INTERVALS[] = { 1, 5, 10, 15, 30, 60, 120, 240, 480 };
static const char *INTERVAL_OPTS = "1 min\n5 min\n10 min\n15 min\n30 min\n1 h\n2 h\n4 h\n8 h";
static const char *RES[] = { "SVGA", "XGA", "HD", "SXGA", "UXGA", "QXGA" };
static const char *RES_OPTS = "SVGA (800×600)\nXGA (1024×768)\nHD (1280×720)\nSXGA (1280×1024)\n"
                              "UXGA (1600×1200)\nQXGA (2048×1536)";
static const char *MODES[] = { "hsv", "hybrid", "model" };
static const char *MODE_OPTS = "Solo color (HSV)\nHíbrido\nModelo";
static const int QUALITY[] = { 8, 10, 12, 15, 20, 25 };
static const char *QUALITY_OPTS = "8 (máxima)\n10\n12\n15\n20\n25 (liviana)";

typedef enum { P_INTERVAL, P_RES, P_RES_FULL, P_MODE, P_QUALITY, P_COUNT } param_t;

static const char *KEYS[P_COUNT] = { "capture_interval_min", "resolution", "resolution_full",
                                     "analysis_mode", "jpeg_quality" };
static const char *LABELS[P_COUNT] = { "Intervalo de captura", "Resolución de rutina",
                                       "Resolución completa", "Análisis", "Calidad JPEG" };

static struct {
    char node[16];
    uint32_t gen;
    lv_obj_t *tabs;
    /* Resumen */
    lv_obj_t *img, *img_note, *cap_info, *analysis_chip, *analysis, *node_info, *btn_capture, *btn_live;
    lv_image_dsc_t dsc;
    uint16_t *pixels;
    /* Historial */
    lv_obj_t *hist;
    char hist_ids[HIST_N][24];
    cJSON *captures;       /**< última respuesta de /captures (para mostrar una elegida) */
    /* Configuración */
    lv_obj_t *dd[P_COUNT], *src[P_COUNT], *cfg_status, *btn_save, *btn_reset;
    int loaded[P_COUNT];   /**< índice cargado de la API (para mandar solo lo que cambió) */
    void (*on_back)(void);
} s_cd;

/* ---------------------------------------------------------------- utilidades */

static int index_of_int(const int *arr, int n, int v)
{
    int best = 0;
    for (int i = 0; i < n; i++) {
        if (arr[i] == v) {
            return i;
        }
        if (arr[i] < v) {
            best = i;  // el más cercano por debajo si no está en la lista
        }
    }
    return best;
}

static int index_of_str(const char **arr, int n, const char *v)
{
    for (int i = 0; v && i < n; i++) {
        if (strcasecmp(arr[i], v) == 0) {
            return i;
        }
    }
    return 0;
}

static const char *source_text(const char *s)
{
    if (!s) {
        return "";
    }
    return strcmp(s, "override") == 0 ? "propio de esta cámara"
         : strcmp(s, "zone") == 0 ? "de la zona"
         : strcmp(s, "global") == 0 ? "global" : "por defecto";
}

static lv_obj_t *col(lv_obj_t *parent, int w)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, w, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 6, 0);
    return c;
}

/* ---------------------------------------------------------------- resumen */

static void on_image(api_result_t *r, void *ctx)
{
    if ((uint32_t)(uintptr_t)ctx != s_cd.gen) {
        return;
    }
    if (r->status != 200 || !r->pixels) {
        lv_label_set_text(s_cd.img_note, "No se pudo cargar la imagen");
        lv_obj_remove_flag(s_cd.img_note, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    uint16_t *old = s_cd.pixels;
    s_cd.pixels = r->pixels;
    r->pixels = NULL;  // ahora es de esta pantalla
    s_cd.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_cd.dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_cd.dsc.header.w = r->w;
    s_cd.dsc.header.h = r->h;
    s_cd.dsc.header.stride = r->w * 2;
    s_cd.dsc.data_size = (uint32_t)r->w * r->h * 2;
    s_cd.dsc.data = (const uint8_t *)s_cd.pixels;
    lv_image_cache_drop(&s_cd.dsc);
    lv_image_set_src(s_cd.img, &s_cd.dsc);
    lv_obj_remove_flag(s_cd.img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_cd.img_note, LV_OBJ_FLAG_HIDDEN);
    heap_caps_free(old);
}

/** Muestra una captura (elemento de /captures) en el resumen y pide su miniatura. */
static void show_capture(const cJSON *c)
{
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(c, "capture_id"));
    if (!id) {
        return;
    }
    const char *ts = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(c, "timestamp"));
    const char *trig = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(c, "trigger_type"));
    const char *res = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(c, "resolution"));
    const cJSON *bytes = cJSON_GetObjectItemCaseSensitive(c, "image_bytes");
    char when[20];
    ui_ddmm_hhmm(ui_parse_iso(ts), when, sizeof(when));
    lv_label_set_text_fmt(s_cd.cap_info, "%s · %s\n%s · %s · %d KB", id, when, ui_trigger_title(trig),
                          res ? res : "-", cJSON_IsNumber(bytes) ? bytes->valueint / 1024 : 0);

    const cJSON *la = cJSON_GetObjectItemCaseSensitive(c, "local_analysis");
    thumb_info_t info = { 0 };
    if (cJSON_IsObject(la)) {
        info.has_analysis = true;
        info.suspicious = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(la, "suspicious"));
        info.dark_scene = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(la, "dark_scene"));
        info.low_plant = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(la, "low_plant_coverage"));
        double g = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(la, "green_ratio"));
        double yb = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(la, "yellow_brown_ratio"));
        double br = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(la, "brightness"));
        double sc = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(la, "suspicion_score"));
        double th = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(la, "threshold"));
        lv_label_set_text_fmt(s_cd.analysis, "Verde %.0f %% · amarillo/marrón %.0f %%\nBrillo %.0f %% · puntaje "
                              "%.2f (umbral %.2f)", g * 100, yb * 100, br * 100, sc, th);
    } else {
        lv_label_set_text(s_cd.analysis, "Sin análisis local");
    }
    const char *txt;
    uint32_t bg, fg;
    ui_analysis_chip(&info, &txt, &bg, &fg);
    theme_chip_set(s_cd.analysis_chip, txt, bg, fg);

    char path[48];
    snprintf(path, sizeof(path), "/captures/%s/thumb", id);
    lv_label_set_text(s_cd.img_note, "Cargando imagen…");
    lv_obj_remove_flag(s_cd.img_note, LV_OBJ_FLAG_HIDDEN);
    api_get_image(path, on_image, (void *)(uintptr_t)s_cd.gen);
}

static void refresh_node_info(void)
{
    ss_node_t n;
    if (!state_store_node(s_cd.node, &n)) {
        lv_label_set_text(s_cd.node_info, "Sin datos del nodo");
        return;
    }
    const char *chip;
    uint32_t bg, fg;
    ui_node_chip(&n, &chip, &bg, &fg);
    char rssi[24] = "-", sd[24] = "-";
    if (n.rssi) {
        snprintf(rssi, sizeof(rssi), "%d dBm", n.rssi);
    }
    if (n.sd_free_pct >= 0) {
        snprintf(sd, sizeof(sd), "%d %% libre", n.sd_free_pct);
    }
    lv_label_set_text_fmt(s_cd.node_info, "%s · %s · fw %s\nWiFi %s · SD %s · config v%" PRId32, chip,
                          n.camera_type[0] ? n.camera_type : "-", n.firmware[0] ? n.firmware : "-", rssi, sd,
                          n.config_version);
    bool online = ui_node_online(&n);
    cmd_status_t cs = command_status(s_cd.node);
    bool busy = cs.state == CMD_SENT || cs.state == CMD_ACCEPTED;
    theme_button_set(s_cd.btn_capture, busy ? "Capturando…" : "Capturar ahora", online && !busy);
    lv_obj_set_state(s_cd.btn_live, LV_STATE_DISABLED, !online);
}

/* ---------------------------------------------------------------- historial */

static void on_hist_click(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    show_capture(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(s_cd.captures, "items"), i));
    lv_tabview_set_active(s_cd.tabs, 0, LV_ANIM_OFF);
}

static void on_captures(api_result_t *r, void *ctx)
{
    if ((uint32_t)(uintptr_t)ctx != s_cd.gen) {
        return;
    }
    lv_obj_clean(s_cd.hist);
    if (r->status != 200 || !r->body) {
        theme_label(s_cd.hist, TH_FONT_BODY, TH_MUTED, "No se pudo cargar el historial");
        return;
    }
    cJSON_Delete(s_cd.captures);
    s_cd.captures = cJSON_ParseWithLength(r->body, r->len);
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(s_cd.captures, "items");
    int i = 0;
    const cJSON *c;
    cJSON_ArrayForEach(c, items) {
        if (i >= HIST_N) {
            break;
        }
        const char *ts = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(c, "timestamp"));
        const char *trig = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(c, "trigger_type"));
        const char *res = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(c, "resolution"));
        const cJSON *la = cJSON_GetObjectItemCaseSensitive(c, "local_analysis");
        thumb_info_t info = { .has_analysis = cJSON_IsObject(la) };
        if (info.has_analysis) {
            info.suspicious = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(la, "suspicious"));
            info.dark_scene = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(la, "dark_scene"));
            info.low_plant = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(la, "low_plant_coverage"));
        }
        lv_obj_t *row = lv_button_create(s_cd.hist);
        lv_obj_set_size(row, LV_PCT(100), 48);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(row, th_color(TH_ACCENT), LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, th_color(TH_LINE), 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 12, 0);
        char when[20];
        ui_ddmm_hhmm(ui_parse_iso(ts), when, sizeof(when));
        theme_label(row, TH_FONT_BODY, TH_FG, when);
        lv_obj_t *t = theme_label(row, TH_FONT_SMALL, TH_MUTED, "");
        lv_label_set_text_fmt(t, "%s · %s", ui_trigger_title(trig), res ? res : "-");
        lv_obj_set_flex_grow(t, 1);
        const char *txt;
        uint32_t bg, fg;
        ui_analysis_chip(&info, &txt, &bg, &fg);
        theme_chip_set(theme_chip(row), txt, bg, fg);
        lv_obj_add_event_cb(row, on_hist_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        i++;
    }
    if (i == 0) {
        theme_label(s_cd.hist, TH_FONT_BODY, TH_MUTED, "Todavía no hay capturas de esta cámara.");
        lv_label_set_text(s_cd.cap_info, "Sin capturas");
        lv_obj_add_flag(s_cd.img, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    show_capture(cJSON_GetArrayItem(items, 0));
}

/* ---------------------------------------------------------------- configuración (override) */

static int dd_index(param_t p)
{
    return (int)lv_dropdown_get_selected(s_cd.dd[p]);
}

static void on_config(api_result_t *r, void *ctx)
{
    if ((uint32_t)(uintptr_t)ctx != s_cd.gen) {
        return;
    }
    if (r->status != 200 || !r->body) {
        lv_label_set_text_fmt(s_cd.cfg_status, r->status ? "La API respondió %d" : "Sin conexión con la API",
                              r->status);
        return;
    }
    cJSON *root = cJSON_ParseWithLength(r->body, r->len);
    const cJSON *cfg = cJSON_GetObjectItemCaseSensitive(root, "config");
    const cJSON *src = cJSON_GetObjectItemCaseSensitive(root, "sources");
    const cJSON *ver = cJSON_GetObjectItemCaseSensitive(root, "version");
    s_cd.loaded[P_INTERVAL] = index_of_int(INTERVALS, 9,
        (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(cfg, "capture_interval_min")));
    s_cd.loaded[P_RES] = index_of_str(RES, 6, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(cfg, "resolution")));
    s_cd.loaded[P_RES_FULL] = index_of_str(RES, 6,
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(cfg, "resolution_full")));
    s_cd.loaded[P_MODE] = index_of_str(MODES, 3, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(cfg, "analysis_mode")));
    s_cd.loaded[P_QUALITY] = index_of_int(QUALITY, 6,
        (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(cfg, "jpeg_quality")));
    for (int p = 0; p < P_COUNT; p++) {
        lv_dropdown_set_selected(s_cd.dd[p], s_cd.loaded[p]);
        lv_label_set_text(s_cd.src[p], source_text(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(src, KEYS[p]))));
    }
    lv_label_set_text_fmt(s_cd.cfg_status, "Configuración v%d de la API", cJSON_IsNumber(ver) ? ver->valueint : 0);
    cJSON_Delete(root);
}

static void load_config(void)
{
    char path[48];
    snprintf(path, sizeof(path), "/config/nodes/%s", s_cd.node);
    api_get(path, 4096, on_config, (void *)(uintptr_t)s_cd.gen);
}

static void on_saved(api_result_t *r, void *ctx)
{
    if (r->status == 200 || r->status == 204) {
        load_config();  // vuelve a leer: versión nueva y de dónde sale cada valor
        lv_label_set_text(s_cd.cfg_status, "Guardado: la cámara la aplica en segundos");
    } else {
        lv_label_set_text_fmt(s_cd.cfg_status, "No se pudo guardar (HTTP %d)", r->status);
    }
}

static void on_save(lv_event_t *e)
{
    cJSON *o = cJSON_CreateObject();
    int changed = 0;
    for (int p = 0; p < P_COUNT; p++) {
        int i = dd_index(p);
        if (i == s_cd.loaded[p]) {
            continue;
        }
        changed++;
        switch (p) {
        case P_INTERVAL: cJSON_AddNumberToObject(o, KEYS[p], INTERVALS[i]); break;
        case P_RES:
        case P_RES_FULL: cJSON_AddStringToObject(o, KEYS[p], RES[i]); break;
        case P_MODE: cJSON_AddStringToObject(o, KEYS[p], MODES[i]); break;
        case P_QUALITY: cJSON_AddNumberToObject(o, KEYS[p], QUALITY[i]); break;
        default: break;
        }
    }
    if (!changed) {
        lv_label_set_text(s_cd.cfg_status, "No hay cambios para guardar");
        cJSON_Delete(o);
        return;
    }
    char *json = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    char path[64];
    snprintf(path, sizeof(path), "/config/nodes/%s/override", s_cd.node);
    lv_label_set_text(s_cd.cfg_status, "Guardando…");
    api_send("PUT", path, json, on_saved, NULL);
    cJSON_free(json);
}

static void on_reset(lv_event_t *e)
{
    char path[64];
    snprintf(path, sizeof(path), "/config/nodes/%s/override", s_cd.node);
    lv_label_set_text(s_cd.cfg_status, "Volviendo a la configuración global…");
    api_send("DELETE", path, NULL, on_saved, NULL);
}

/* ---------------------------------------------------------------- acciones */

static void on_capture(lv_event_t *e)
{
    command_capture_now(s_cd.node);
}

static void on_live(lv_event_t *e)
{
    ui_shell_open_live(s_cd.node);
}

static void on_back(lv_event_t *e)
{
    if (s_cd.on_back) {
        s_cd.on_back();
    }
}

/* ---------------------------------------------------------------- armado */

static void style_tabs(lv_obj_t *tv)
{
    lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
    lv_obj_t *bar = lv_tabview_get_tab_bar(tv);
    lv_obj_set_style_bg_color(bar, th_color(TH_BG), 0);
    lv_obj_set_style_pad_hor(bar, TH_GAP, 0);
    for (uint32_t i = 0; i < lv_obj_get_child_count(bar); i++) {
        lv_obj_t *b = lv_obj_get_child(bar, i);
        lv_obj_set_style_text_font(b, theme_font(TH_FONT_BODY), 0);
        lv_obj_set_style_text_color(b, th_color(TH_MUTED), 0);
        lv_obj_set_style_text_color(b, th_color(TH_FG), LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(b, th_color(TH_OK_FG), LV_STATE_CHECKED);
    }
}

static lv_obj_t *tab_page(lv_obj_t *tv, const char *name)
{
    lv_obj_t *t = lv_tabview_add_tab(tv, name);
    lv_obj_set_style_pad_all(t, 0, 0);
    lv_obj_set_style_pad_hor(t, TH_GAP, 0);
    lv_obj_set_style_pad_top(t, 8, 0);
    return t;
}

static void build_summary(lv_obj_t *t)
{
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *frame = lv_obj_create(t);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, IMG_W, IMG_H);
    lv_obj_set_style_bg_color(frame, th_color(TH_CARD_ALT), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(frame, 8, 0);
    s_cd.img_note = theme_label(frame, TH_FONT_SMALL, TH_MUTED, "Cargando imagen…");
    lv_obj_center(s_cd.img_note);
    s_cd.img = lv_image_create(frame);
    lv_obj_set_size(s_cd.img, IMG_W, IMG_H);
    lv_image_set_inner_align(s_cd.img, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_add_flag(s_cd.img, LV_OBJ_FLAG_HIDDEN);
    s_cd.analysis_chip = theme_chip(frame);
    lv_obj_align(s_cd.analysis_chip, LV_ALIGN_BOTTOM_LEFT, 8, -8);

    lv_obj_t *right = col(t, SCR_W - IMG_W - 3 * TH_GAP);
    lv_obj_set_pos(right, IMG_W + TH_GAP, 0);
    theme_label(right, TH_FONT_SMALL, TH_MUTED, "CAPTURA");
    s_cd.cap_info = theme_label(right, TH_FONT_BODY, TH_FG, "");
    theme_label(right, TH_FONT_SMALL, TH_MUTED, "ANÁLISIS LOCAL");
    s_cd.analysis = theme_label(right, TH_FONT_BODY, TH_FG, "");
    theme_label(right, TH_FONT_SMALL, TH_MUTED, "NODO");
    s_cd.node_info = theme_label(right, TH_FONT_BODY, TH_FG, "");

    lv_obj_t *btns = lv_obj_create(right);
    lv_obj_remove_style_all(btns);
    lv_obj_set_size(btns, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(btns, 8, 0);
    lv_obj_set_style_margin_top(btns, 6, 0);
    s_cd.btn_capture = theme_button(btns, "Capturar ahora");
    lv_obj_set_flex_grow(s_cd.btn_capture, 1);
    lv_obj_add_event_cb(s_cd.btn_capture, on_capture, LV_EVENT_CLICKED, NULL);
    s_cd.btn_live = theme_button_secondary(btns, LV_SYMBOL_PLAY " En vivo");
    lv_obj_set_width(s_cd.btn_live, 112);
    lv_obj_add_event_cb(s_cd.btn_live, on_live, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back = theme_button_secondary(btns, LV_SYMBOL_LEFT);
    lv_obj_set_width(back, 56);
    lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, NULL);
}

static void build_config(lv_obj_t *t)
{
    lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(t, 6, 0);
    const char *opts[P_COUNT] = { INTERVAL_OPTS, RES_OPTS, RES_OPTS, MODE_OPTS, QUALITY_OPTS };
    for (int p = 0; p < P_COUNT; p++) {
        lv_obj_t *r = lv_obj_create(t);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(r, 12, 0);
        lv_obj_t *l = theme_label(r, TH_FONT_BODY, TH_FG, LABELS[p]);
        lv_obj_set_width(l, 200);
        s_cd.dd[p] = theme_dropdown(r, opts[p]);
        lv_obj_set_width(s_cd.dd[p], 240);
        s_cd.src[p] = theme_label(r, TH_FONT_SMALL, TH_MUTED, "");
    }
    lv_obj_t *r = lv_obj_create(t);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 8, 0);
    s_cd.btn_save = theme_button(r, "Guardar");
    lv_obj_set_width(s_cd.btn_save, 140);
    lv_obj_add_event_cb(s_cd.btn_save, on_save, LV_EVENT_CLICKED, NULL);
    s_cd.btn_reset = theme_button_secondary(r, "Usar la global");
    lv_obj_set_width(s_cd.btn_reset, 170);
    lv_obj_add_event_cb(s_cd.btn_reset, on_reset, LV_EVENT_CLICKED, NULL);
    s_cd.cfg_status = theme_label(r, TH_FONT_SMALL, TH_MUTED, "");
}

void scr_camera_create(lv_obj_t *p, void (*on_back_cb)(void))
{
    s_cd.on_back = on_back_cb;
    s_cd.tabs = lv_tabview_create(p);
    lv_tabview_set_tab_bar_size(s_cd.tabs, 44);
    lv_obj_set_size(s_cd.tabs, SCR_W, SCR_H);
    build_summary(tab_page(s_cd.tabs, "Resumen"));
    lv_obj_t *h = tab_page(s_cd.tabs, "Historial");
    s_cd.hist = h;
    lv_obj_set_flex_flow(h, LV_FLEX_FLOW_COLUMN);
    build_config(tab_page(s_cd.tabs, "Configuración"));
    style_tabs(s_cd.tabs);
}

void scr_camera_enter(const char *node_id)
{
    strlcpy(s_cd.node, node_id, sizeof(s_cd.node));
    s_cd.gen++;
    lv_tabview_set_active(s_cd.tabs, 0, LV_ANIM_OFF);
    lv_obj_add_flag(s_cd.img, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_cd.cap_info, "Cargando…");
    lv_label_set_text(s_cd.analysis, "");
    lv_label_set_text(s_cd.cfg_status, "Cargando configuración…");
    refresh_node_info();
    char path[64];
    snprintf(path, sizeof(path), "/captures?node_id=%s&limit=%d", node_id, HIST_N);
    api_get(path, 24 * 1024, on_captures, (void *)(uintptr_t)s_cd.gen);
    load_config();
}

void scr_camera_refresh(uint32_t dirty)
{
    if (dirty & (UI_DIRTY_NODES | UI_DIRTY_CMD | UI_DIRTY_TICK)) {
        refresh_node_info();
    }
    if ((dirty & UI_DIRTY_THUMB) && dirty != UI_DIRTY_ALL) {
        /* Llegó una captura nueva de alguna cámara: si es de esta, actualizar el historial */
        char path[64];
        snprintf(path, sizeof(path), "/captures?node_id=%s&limit=%d", s_cd.node, HIST_N);
        api_get(path, 24 * 1024, on_captures, (void *)(uintptr_t)s_cd.gen);
    }
}
