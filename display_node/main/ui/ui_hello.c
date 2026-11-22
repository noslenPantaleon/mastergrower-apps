/**
 * @file ui_hello.c
 * @brief Ver ui_hello.h.
 */
#include "ui_hello.h"

#include "lvgl.h"
#include "sdkconfig.h"

#define C_BG      0x121512
#define C_CARD    0x1b201a
#define C_FG      0xe6ebe4
#define C_MUTED   0x94a08f
#define C_ACCENT  0x7cc47f

static lv_obj_t *s_count_label;
static unsigned s_taps;

static void on_tap(lv_event_t *e)
{
    lv_label_set_text_fmt(s_count_label, "Toques: %u", ++s_taps);
}

static void anim_x(void *obj, int32_t v)
{
    lv_obj_set_x(obj, v);
}

void ui_hello_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(C_FG), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "MasterGrow");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_36, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 32, 32);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, CONFIG_MG_NODE_ID " · nodo de pantalla · D0");
    lv_obj_set_style_text_color(sub, lv_color_hex(C_MUTED), 0);
    lv_obj_align_to(sub, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 240, 88);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(C_FG), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_add_event_cb(btn, on_tap, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, "Tocar");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(bl, lv_color_hex(C_BG), 0);
    lv_obj_center(bl);

    s_count_label = lv_label_create(scr);
    lv_label_set_text(s_count_label, "Toques: 0");
    lv_obj_align_to(s_count_label, btn, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);

    /* Barra vertical que cruza la pantalla: con tearing se vería partida */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 24, 96);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, -24);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_exec_cb(&a, anim_x);
    lv_anim_set_values(&a, 0, 800 - 24);
    lv_anim_set_duration(&a, 1600);
    lv_anim_set_playback_duration(&a, 1600);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}
