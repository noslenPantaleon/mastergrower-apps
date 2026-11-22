/**
 * @file theme.c
 * @brief Ver theme.h.
 */
#include "theme.h"

#include "esp_log.h"

static const char *TAG = "theme";

extern const uint8_t montserrat_ttf_start[] asm("_binary_Montserrat_Medium_ttf_start");
extern const uint8_t montserrat_ttf_end[] asm("_binary_Montserrat_Medium_ttf_end");

static const struct {
    int px;
    const lv_font_t *fallback;  /**< para los LV_SYMBOL_* */
    size_t cache;               /**< glifos en caché */
} FONT_SPEC[TH_FONT_COUNT] = {
    [TH_FONT_SMALL] = { 14, &lv_font_montserrat_14, 160 },
    [TH_FONT_BODY]  = { 16, &lv_font_montserrat_14, 160 },
    [TH_FONT_TITLE] = { 20, &lv_font_montserrat_20, 128 },
    [TH_FONT_H2]    = { 28, &lv_font_montserrat_28, 64 },
    [TH_FONT_KPI]   = { 40, &lv_font_montserrat_36, 48 },
};

static lv_font_t *s_fonts[TH_FONT_COUNT];

void theme_init(void)
{
    size_t len = montserrat_ttf_end - montserrat_ttf_start;
    for (int i = 0; i < TH_FONT_COUNT; i++) {
        s_fonts[i] = lv_tiny_ttf_create_data_ex(montserrat_ttf_start, len, FONT_SPEC[i].px,
                                                LV_FONT_KERNING_NORMAL, FONT_SPEC[i].cache);
        if (s_fonts[i]) {
            s_fonts[i]->fallback = FONT_SPEC[i].fallback;
        } else {
            ESP_LOGE(TAG, "No se pudo crear la fuente de %d px", FONT_SPEC[i].px);
        }
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, th_color(TH_BG), 0);
    lv_obj_set_style_text_color(scr, th_color(TH_FG), 0);
    lv_obj_set_style_text_font(scr, theme_font(TH_FONT_BODY), 0);
}

const lv_font_t *theme_font(th_font_t f)
{
    static const lv_font_t *builtin[TH_FONT_COUNT] = {
        &lv_font_montserrat_14, &lv_font_montserrat_14, &lv_font_montserrat_20,
        &lv_font_montserrat_28, &lv_font_montserrat_36,
    };
    return s_fonts[f] ? s_fonts[f] : builtin[f];
}

void theme_card(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, th_color(TH_CARD), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, TH_RADIUS, 0);
    lv_obj_set_style_pad_all(obj, 16, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *theme_label(lv_obj_t *parent, th_font_t font, uint32_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, theme_font(font), 0);
    lv_obj_set_style_text_color(l, th_color(color), 0);
    lv_label_set_text(l, text ? text : "");
    return l;
}

lv_obj_t *theme_chip(lv_obj_t *parent)
{
    lv_obj_t *c = lv_label_create(parent);
    lv_obj_set_style_text_font(c, theme_font(TH_FONT_SMALL), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 10, 0);
    lv_obj_set_style_pad_hor(c, 10, 0);
    lv_obj_set_style_pad_ver(c, 3, 0);
    theme_chip_set(c, "", TH_OFF_BG, TH_OFF_FG);
    return c;
}

void theme_chip_set(lv_obj_t *chip, const char *text, uint32_t bg, uint32_t fg)
{
    lv_label_set_text(chip, text);
    lv_obj_set_style_bg_color(chip, th_color(bg), 0);
    lv_obj_set_style_text_color(chip, th_color(fg), 0);
}

lv_obj_t *theme_button(lv_obj_t *parent, const char *text)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_height(b, 48);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, th_color(TH_ACCENT), 0);
    lv_obj_set_style_bg_color(b, th_color(TH_OFF_BG), LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(b, lv_color_lighten(th_color(TH_ACCENT), 40), LV_STATE_PRESSED);
    lv_obj_t *l = theme_label(b, TH_FONT_BODY, TH_ACCENT_FG, text);
    lv_obj_set_style_text_color(l, th_color(TH_OFF_FG), LV_STATE_DISABLED);
    lv_obj_center(l);
    return b;
}

void theme_button_set(lv_obj_t *btn, const char *text, bool enabled)
{
    lv_label_set_text(lv_obj_get_child(btn, 0), text);
    if (enabled) {
        lv_obj_remove_state(btn, LV_STATE_DISABLED);
        lv_obj_remove_state(lv_obj_get_child(btn, 0), LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(btn, LV_STATE_DISABLED);
        lv_obj_add_state(lv_obj_get_child(btn, 0), LV_STATE_DISABLED);
    }
}

lv_obj_t *theme_button_secondary(lv_obj_t *parent, const char *text)
{
    lv_obj_t *b = theme_button(parent, text);
    lv_obj_set_style_bg_color(b, th_color(TH_CARD_ALT), 0);
    lv_obj_set_style_bg_color(b, th_color(TH_LINE), LV_STATE_PRESSED);
    return b;
}

lv_obj_t *theme_dropdown(lv_obj_t *parent, const char *options)
{
    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_dropdown_set_options(dd, options);
    lv_obj_set_height(dd, 44);
    lv_obj_set_style_bg_color(dd, th_color(TH_CARD_ALT), 0);
    lv_obj_set_style_border_width(dd, 0, 0);
    lv_obj_set_style_radius(dd, 8, 0);
    lv_obj_set_style_text_color(dd, th_color(TH_FG), 0);
    lv_obj_set_style_text_font(dd, theme_font(TH_FONT_BODY), 0);
    lv_obj_set_style_pad_ver(dd, 10, 0);
    lv_obj_t *list = lv_dropdown_get_list(dd);
    lv_obj_set_style_bg_color(list, th_color(TH_CARD_ALT), 0);
    lv_obj_set_style_border_color(list, th_color(TH_LINE), 0);
    lv_obj_set_style_text_color(list, th_color(TH_FG), 0);
    lv_obj_set_style_text_font(list, theme_font(TH_FONT_BODY), 0);
    lv_obj_set_style_bg_color(list, th_color(TH_ACCENT), LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(list, th_color(TH_ACCENT), LV_PART_SELECTED | LV_STATE_PRESSED);
    lv_obj_set_style_max_height(list, 240, 0);
    return dd;
}
