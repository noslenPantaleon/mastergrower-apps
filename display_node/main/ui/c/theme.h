/**
 * @file theme.h
 * @brief Tema oscuro de alto contraste (SRS de pantalla §3.1, maqueta de la Arquitectura Fig. 3)
 *        sobre una grilla de 8 px, y fuentes Montserrat con Latin-1 (tildes y ñ) dibujadas con
 *        TinyTTF; los íconos LV_SYMBOL_* salen de las Montserrat incluidas en LVGL (respaldo).
 */
#pragma once

#include <stdint.h>
#include "lvgl.h"

/* Paleta */
#define TH_BG        0x16201a  /**< fondo */
#define TH_BAR       0x111913  /**< barras superior e inferior */
#define TH_CARD      0x1f2c22
#define TH_CARD_ALT  0x263529  /**< zonas dentro de una tarjeta (miniaturas, gráficos) */
#define TH_LINE      0x2c3b2f
#define TH_FG        0xe6ede4
#define TH_MUTED     0x9aa89a
#define TH_ACCENT    0x2e6b33  /**< botones y selección */
#define TH_ACCENT_FG 0xe6f2e4
#define TH_OK_BG     0x2e6b33
#define TH_OK_FG     0xd7ecd5
#define TH_WARN_BG   0xa8861c
#define TH_WARN_FG   0x1b1606
#define TH_BAD_BG    0xc4473a
#define TH_BAD_FG    0xfbe9e6
#define TH_OFF_BG    0x3a3f3b  /**< deshabilitado / sin datos */
#define TH_OFF_FG    0xb4bcb3
#define TH_SERIES_1  0x6aa3f0  /**< gráfico: temperatura */
#define TH_SERIES_2  0x79c97b  /**< gráfico: humedad */

#define TH_GAP     16   /**< separación entre tarjetas (2 × grilla) */
#define TH_RADIUS  12
#define TH_TOPBAR_H  48
#define TH_NAVBAR_H  72

typedef enum {
    TH_FONT_SMALL,   /**< 14 px: detalles */
    TH_FONT_BODY,    /**< 16 px: texto general */
    TH_FONT_TITLE,   /**< 20 px: títulos y nombres */
    TH_FONT_H2,      /**< 28 px */
    TH_FONT_KPI,     /**< 40 px: valores de KPI */
    TH_FONT_COUNT
} th_font_t;

void theme_init(void);
const lv_font_t *theme_font(th_font_t f);

static inline lv_color_t th_color(uint32_t hex) { return lv_color_hex(hex); }

/** @brief Tarjeta: fondo, radio, sin borde, relleno de 16 px, sin scroll. */
void theme_card(lv_obj_t *obj);

/** @brief Etiqueta con fuente y color. */
lv_obj_t *theme_label(lv_obj_t *parent, th_font_t font, uint32_t color, const char *text);

/** @brief Chip (pastilla de estado). */
lv_obj_t *theme_chip(lv_obj_t *parent);
void theme_chip_set(lv_obj_t *chip, const char *text, uint32_t bg, uint32_t fg);

/** @brief Botón principal con una etiqueta (primer hijo). */
lv_obj_t *theme_button(lv_obj_t *parent, const char *text);
void theme_button_set(lv_obj_t *btn, const char *text, bool enabled);
