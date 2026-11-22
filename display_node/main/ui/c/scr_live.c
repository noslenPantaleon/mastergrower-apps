/**
 * @file scr_live.c
 * @brief Vista en vivo (SRS de pantalla F-05): video 480×360 (4:3), estado y fps reales, guías de
 *        encuadre (tercios, cruz, grilla), resolución y fps máximos, captura completa y volver.
 *        Un timer de LVGL toma el cuadro más nuevo cada 15 ms; la sesión se cierra al salir.
 */
#include <stdio.h>
#include <string.h>
#include "command_publisher.h"
#include "esp_timer.h"
#include "live_view.h"
#include "screens.h"
#include "theme.h"
#include "ui_common.h"

#define VIDEO_W  480
#define VIDEO_H  360
#define PANEL_X  (VIDEO_W + TH_GAP)
#define PANEL_W  (SCR_W - PANEL_X - TH_GAP)

static const char *RES[] = { "qvga", "vga" };
static const int FPS[] = { 5, 10, 15 };

static struct {
    char node_id[16];
    int res_i, fps_i;
    lv_obj_t *video, *img, *overlay_msg;
    lv_obj_t *guides[3];          /**< tercios, cruz, grilla */
    lv_obj_t *title, *status, *stats;
    lv_obj_t *res_btn[2], *fps_btn[3], *guide_btn[3], *capture_btn, *retry_btn;
    lv_image_dsc_t dsc;
    lv_timer_t *timer;
    uint32_t shown;
    int64_t fps_t0;
    uint32_t fps_n;
    float fps_shown;
    void (*on_back)(void);
} s_lv;

/* ---------------------------------------------------------------- guías */

static void line(lv_obj_t *parent, lv_point_precise_t *pts)
{
    lv_obj_t *l = lv_line_create(parent);
    lv_line_set_points(l, pts, 2);
    lv_obj_set_style_line_color(l, lv_color_white(), 0);
    lv_obj_set_style_line_opa(l, LV_OPA_60, 0);
    lv_obj_set_style_line_width(l, 1, 0);
}

static lv_obj_t *guide_layer(void)
{
    lv_obj_t *g = lv_obj_create(s_lv.video);
    lv_obj_remove_style_all(g);
    lv_obj_set_size(g, VIDEO_W, VIDEO_H);
    lv_obj_add_flag(g, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g, LV_OBJ_FLAG_CLICKABLE);
    return g;
}

static void build_guides(void)
{
    /* Los puntos tienen que vivir mientras existan las líneas */
    static lv_point_precise_t thirds[4][2] = {
        { { VIDEO_W / 3, 0 }, { VIDEO_W / 3, VIDEO_H } },
        { { 2 * VIDEO_W / 3, 0 }, { 2 * VIDEO_W / 3, VIDEO_H } },
        { { 0, VIDEO_H / 3 }, { VIDEO_W, VIDEO_H / 3 } },
        { { 0, 2 * VIDEO_H / 3 }, { VIDEO_W, 2 * VIDEO_H / 3 } },
    };
    static lv_point_precise_t cross[2][2] = {
        { { VIDEO_W / 2 - 24, VIDEO_H / 2 }, { VIDEO_W / 2 + 24, VIDEO_H / 2 } },
        { { VIDEO_W / 2, VIDEO_H / 2 - 24 }, { VIDEO_W / 2, VIDEO_H / 2 + 24 } },
    };
    static lv_point_precise_t grid[10][2];
    for (int i = 1; i <= 5; i++) {
        grid[i - 1][0] = (lv_point_precise_t){ i * VIDEO_W / 6, 0 };
        grid[i - 1][1] = (lv_point_precise_t){ i * VIDEO_W / 6, VIDEO_H };
    }
    for (int i = 1; i <= 5; i++) {
        grid[4 + i][0] = (lv_point_precise_t){ 0, i * VIDEO_H / 6 };
        grid[4 + i][1] = (lv_point_precise_t){ VIDEO_W, i * VIDEO_H / 6 };
    }
    s_lv.guides[0] = guide_layer();
    for (int i = 0; i < 4; i++) {
        line(s_lv.guides[0], thirds[i]);
    }
    s_lv.guides[1] = guide_layer();
    for (int i = 0; i < 2; i++) {
        line(s_lv.guides[1], cross[i]);
    }
    s_lv.guides[2] = guide_layer();
    for (int i = 0; i < 10; i++) {
        line(s_lv.guides[2], grid[i]);
    }
}

/* ---------------------------------------------------------------- controles */

static lv_obj_t *toggle(lv_obj_t *parent, const char *text, lv_event_cb_t cb, intptr_t idx)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_height(b, 40);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, th_color(TH_CARD_ALT), 0);
    lv_obj_set_style_bg_color(b, th_color(TH_ACCENT), LV_STATE_CHECKED);
    lv_obj_t *l = theme_label(b, TH_FONT_SMALL, TH_FG, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)idx);
    return b;
}

static lv_obj_t *row(lv_obj_t *parent, const char *label)
{
    theme_label(parent, TH_FONT_SMALL, TH_MUTED, label);
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r, 6, 0);
    return r;
}

static void restart(void)
{
    s_lv.shown = 0;
    s_lv.fps_shown = 0;
    lv_obj_add_flag(s_lv.img, LV_OBJ_FLAG_HIDDEN);
    live_view_release_frames();
    live_view_start(s_lv.node_id, RES[s_lv.res_i], FPS[s_lv.fps_i]);
}

static void sync_toggles(void)
{
    for (int i = 0; i < 2; i++) {
        lv_obj_set_state(s_lv.res_btn[i], LV_STATE_CHECKED, i == s_lv.res_i);
    }
    for (int i = 0; i < 3; i++) {
        lv_obj_set_state(s_lv.fps_btn[i], LV_STATE_CHECKED, i == s_lv.fps_i);
        lv_obj_set_state(s_lv.guide_btn[i], LV_STATE_CHECKED,
                         !lv_obj_has_flag(s_lv.guides[i], LV_OBJ_FLAG_HIDDEN));
    }
}

static void on_res(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i != s_lv.res_i) {
        s_lv.res_i = i;
        restart();
    }
    sync_toggles();
}

static void on_fps(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i != s_lv.fps_i) {
        s_lv.fps_i = i;
        restart();
    }
    sync_toggles();
}

static void on_guide(lv_event_t *e)
{
    lv_obj_t *g = s_lv.guides[(int)(intptr_t)lv_event_get_user_data(e)];
    lv_obj_set_flag(g, LV_OBJ_FLAG_HIDDEN, !lv_obj_has_flag(g, LV_OBJ_FLAG_HIDDEN));
    sync_toggles();
}

static void on_capture(lv_event_t *e)
{
    command_capture_now(s_lv.node_id);  // la cámara pausa el stream, captura completa y retoma
}

static void on_retry(lv_event_t *e)
{
    restart();
}

static void on_back(lv_event_t *e)
{
    if (s_lv.on_back) {
        s_lv.on_back();
    }
}

/* ---------------------------------------------------------------- cuadros */

static void tick(lv_timer_t *t)
{
    const uint16_t *px;
    uint16_t w, h;
    int64_t now = esp_timer_get_time();
    if (live_view_take_frame(&px, &w, &h)) {
        s_lv.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        s_lv.dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        s_lv.dsc.header.w = w;
        s_lv.dsc.header.h = h;
        s_lv.dsc.header.stride = w * 2;
        s_lv.dsc.data_size = (uint32_t)w * h * 2;
        s_lv.dsc.data = (const uint8_t *)px;
        lv_image_cache_drop(&s_lv.dsc);
        lv_image_set_src(s_lv.img, &s_lv.dsc);
        lv_obj_remove_flag(s_lv.img, LV_OBJ_FLAG_HIDDEN);
        s_lv.shown++;
    }
    if (now - s_lv.fps_t0 >= 1000000) {
        s_lv.fps_shown = (s_lv.shown - s_lv.fps_n) * 1e6f / (now - s_lv.fps_t0);
        s_lv.fps_n = s_lv.shown;
        s_lv.fps_t0 = now;
    }

    live_status_t st = live_view_status();
    bool ended = st.state == LIVE_ENDED || st.state == LIVE_ERROR;
    const char *overlay = st.state == LIVE_STREAMING ? (st.msg[0] ? st.msg : NULL)
                          : st.msg[0] ? st.msg : "Conectando…";
    lv_obj_set_flag(s_lv.overlay_msg, LV_OBJ_FLAG_HIDDEN, overlay == NULL);
    if (overlay) {
        lv_label_set_text(s_lv.overlay_msg, overlay);
    }
    lv_obj_set_flag(s_lv.retry_btn, LV_OBJ_FLAG_HIDDEN, !ended);
    if (st.state == LIVE_STREAMING) {
        lv_label_set_text_fmt(s_lv.status, LV_SYMBOL_PLAY "  En vivo · %.1f fps", (double)s_lv.fps_shown);
        lv_obj_set_style_text_color(s_lv.status, th_color(TH_OK_FG), 0);
    } else {
        lv_label_set_text(s_lv.status, ended ? "Detenido" : "Conectando…");
        lv_obj_set_style_text_color(s_lv.status, th_color(ended ? TH_BAD_BG : TH_MUTED), 0);
    }
    char first[24] = "";
    if (st.first_frame_ms) {
        snprintf(first, sizeof(first), " · 1.º cuadro %.1f s", st.first_frame_ms / 1000.0);
    }
    lv_label_set_text_fmt(s_lv.stats, "%s · %d fps máx · recibe %.1f fps\n%" PRIu32 " cuadros · %" PRIu32
                          " descartados%s", st.resolution[0] ? st.resolution : RES[s_lv.res_i],
                          st.fps_max ? st.fps_max : FPS[s_lv.fps_i], (double)st.fps_rx, st.frames,
                          st.dropped, first);

    cmd_status_t cs = command_status(s_lv.node_id);
    bool busy = cs.state == CMD_SENT || cs.state == CMD_ACCEPTED;
    bool done = cs.state == CMD_DONE && now - cs.updated_us < 4000000;
    theme_button_set(s_lv.capture_btn, busy ? "Capturando…" : done ? LV_SYMBOL_OK " Captura lista"
                     : "Captura completa", !busy && !done && st.state == LIVE_STREAMING);
}

/* ---------------------------------------------------------------- armado y ciclo de vida */

void scr_live_create(lv_obj_t *p, void (*on_back_cb)(void))
{
    s_lv.on_back = on_back_cb;
    s_lv.fps_i = 1;  // 10 fps

    s_lv.video = lv_obj_create(p);
    lv_obj_remove_style_all(s_lv.video);
    lv_obj_set_size(s_lv.video, VIDEO_W, VIDEO_H);
    lv_obj_set_style_bg_color(s_lv.video, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_lv.video, LV_OPA_COVER, 0);
    s_lv.img = lv_image_create(s_lv.video);
    lv_obj_set_size(s_lv.img, VIDEO_W, VIDEO_H);
    lv_image_set_inner_align(s_lv.img, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_add_flag(s_lv.img, LV_OBJ_FLAG_HIDDEN);
    build_guides();
    s_lv.overlay_msg = theme_label(s_lv.video, TH_FONT_BODY, TH_FG, "");
    lv_obj_set_width(s_lv.overlay_msg, VIDEO_W - 48);
    lv_label_set_long_mode(s_lv.overlay_msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_lv.overlay_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_lv.overlay_msg);
    s_lv.retry_btn = theme_button(s_lv.video, "Reintentar");
    lv_obj_set_width(s_lv.retry_btn, 160);
    lv_obj_align(s_lv.retry_btn, LV_ALIGN_CENTER, 0, 56);
    lv_obj_add_event_cb(s_lv.retry_btn, on_retry, LV_EVENT_CLICKED, NULL);

    lv_obj_t *panel = lv_obj_create(p);
    theme_card(panel);
    lv_obj_set_size(panel, PANEL_W, SCR_H - 2 * TH_GAP + 16);
    lv_obj_set_pos(panel, PANEL_X, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, 6, 0);
    s_lv.status = theme_label(panel, TH_FONT_TITLE, TH_MUTED, "Conectando…");
    s_lv.stats = theme_label(panel, TH_FONT_SMALL, TH_MUTED, "");

    lv_obj_t *r = row(panel, "Resolución");
    s_lv.res_btn[0] = toggle(r, "QVGA", on_res, 0);
    s_lv.res_btn[1] = toggle(r, "VGA", on_res, 1);
    r = row(panel, "Cuadros por segundo (máx.)");
    for (int i = 0; i < 3; i++) {
        char t[8];
        snprintf(t, sizeof(t), "%d", FPS[i]);
        s_lv.fps_btn[i] = toggle(r, t, on_fps, i);
    }
    r = row(panel, "Guías de encuadre");
    s_lv.guide_btn[0] = toggle(r, "Tercios", on_guide, 0);
    s_lv.guide_btn[1] = toggle(r, "Cruz", on_guide, 1);
    s_lv.guide_btn[2] = toggle(r, "Grilla", on_guide, 2);

    lv_obj_t *actions = lv_obj_create(panel);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, 6, 0);
    lv_obj_set_style_margin_top(actions, 4, 0);
    s_lv.capture_btn = theme_button(actions, "Captura completa");
    lv_obj_set_flex_grow(s_lv.capture_btn, 1);
    lv_obj_add_event_cb(s_lv.capture_btn, on_capture, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back = theme_button(actions, LV_SYMBOL_LEFT " Volver");
    lv_obj_set_style_bg_color(back, th_color(TH_CARD_ALT), 0);
    lv_obj_set_width(back, 96);
    lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, NULL);

    lv_obj_remove_flag(s_lv.guides[0], LV_OBJ_FLAG_HIDDEN);  // tercios por defecto
    sync_toggles();
}

void scr_live_enter(const char *node_id)
{
    strlcpy(s_lv.node_id, node_id, sizeof(s_lv.node_id));
    s_lv.fps_t0 = esp_timer_get_time();
    s_lv.fps_n = 0;
    restart();
    if (!s_lv.timer) {
        s_lv.timer = lv_timer_create(tick, 15, NULL);
    }
    tick(NULL);
}

void scr_live_leave(void)
{
    if (s_lv.timer) {
        lv_timer_delete(s_lv.timer);
        s_lv.timer = NULL;
    }
    live_view_stop();
    lv_obj_add_flag(s_lv.img, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_src(s_lv.img, NULL);
    live_view_release_frames();
}
