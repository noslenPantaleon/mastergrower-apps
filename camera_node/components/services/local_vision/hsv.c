/**
 * @file hsv.c
 * @brief Ver local_vision.h. Todo en enteros por píxel (matiz en grados, S y V en 0–255).
 */
#include "local_vision.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "jpeg_decoder.h"

static const char *TAG = "local_vision";

/* Clasificación por píxel. Matiz en grados (0–359), S y V en 0–255. */
#define V_DARK        40     /**< por debajo: píxel oscuro, sin color confiable */
#define S_MIN_COLOR   40     /**< por debajo (~16 %): gris/blanco, no cuenta como planta */
#define H_GREEN_MIN   65
#define H_GREEN_MAX   175    /**< las hojas oscuras a contraluz tiran a verde azulado */
#define H_YELLOW_MIN  40     /**< 40–70: amarillo (clorosis) */
#define H_BROWN_MIN   10     /**< 10–40 con V moderado: marrón (hoja seca) */
#define V_BROWN_MAX   200    /**< naranja brillante no es marrón */
#define S_MIN_YB      77     /**< amarillo/marrón exigen algo más de saturación (~30 %) */

/* Auto-niveles por canal antes de clasificar: con contraluz (ventana detrás) la lente
 * suma un velo que sube el negro a ~70/255 y deja las hojas con ~7 % de saturación.
 * Estirar cada canal entre sus percentiles 2 y 98 lo quita y corrige de paso un poco el
 * balance de blancos. Medido con fotos reales: saturación de las hojas 25 → 50 (mediana). */
#define LEVELS_LOW_PCT   2
#define LEVELS_HIGH_PCT  98
#define LEVELS_MIN_SPAN  48  /**< imagen casi plana (oscura, tapada): no estirar el ruido */

static void build_levels(const uint8_t *rgb, uint32_t n, uint8_t lut[3][256])
{
    uint32_t *hist = heap_caps_calloc(3 * 256, sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    for (int c = 0; c < 3; c++) {
        for (int x = 0; x < 256; x++) {
            lut[c][x] = (uint8_t)x;
        }
    }
    if (!hist) {
        return;  /* sin memoria: se clasifica sin corregir */
    }
    for (uint32_t i = 0; i < n; i++) {
        hist[rgb[3 * i]]++;
        hist[256 + rgb[3 * i + 1]]++;
        hist[512 + rgb[3 * i + 2]]++;
    }
    for (int c = 0; c < 3; c++) {
        const uint32_t *hc = hist + 256 * c;
        uint32_t lo_n = n * LEVELS_LOW_PCT / 100, hi_n = n * LEVELS_HIGH_PCT / 100, acc = 0;
        int lo = 0, hi = 255;
        for (int x = 0; x < 256; x++) {
            uint32_t prev = acc;
            acc += hc[x];
            if (prev <= lo_n && acc > lo_n) {
                lo = x;
            }
            if (prev < hi_n && acc >= hi_n) {
                hi = x;
                break;
            }
        }
        if (hi - lo < LEVELS_MIN_SPAN) {
            continue;
        }
        for (int x = 0; x < 256; x++) {
            int v = (x - lo) * 255 / (hi - lo);
            lut[c][x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
    }
    heap_caps_free(hist);
}

typedef enum { PX_OTHER, PX_DARK, PX_GREEN, PX_YELLOW_BROWN } px_class_t;

static px_class_t classify(uint8_t r, uint8_t g, uint8_t b, uint8_t *v_out)
{
    uint8_t max = r > g ? (r > b ? r : b) : (g > b ? g : b);
    uint8_t min = r < g ? (r < b ? r : b) : (g < b ? g : b);
    *v_out = max;
    if (max < V_DARK) {
        return PX_DARK;
    }
    int delta = max - min;
    int s = max ? delta * 255 / max : 0;
    if (s < S_MIN_COLOR || delta == 0) {
        return PX_OTHER;
    }
    int h;
    if (max == r) {
        h = 60 * (g - b) / delta;
    } else if (max == g) {
        h = 120 + 60 * (b - r) / delta;
    } else {
        h = 240 + 60 * (r - g) / delta;
    }
    if (h < 0) {
        h += 360;
    }
    if (h >= H_GREEN_MIN && h <= H_GREEN_MAX) {
        return PX_GREEN;
    }
    if (s >= S_MIN_YB) {
        if (h >= H_YELLOW_MIN && h < H_GREEN_MIN) {
            return PX_YELLOW_BROWN;
        }
        if (h >= H_BROWN_MIN && h < H_YELLOW_MIN && max <= V_BROWN_MAX) {
            return PX_YELLOW_BROWN;
        }
    }
    return PX_OTHER;
}

static esp_jpeg_image_scale_t pick_scale(uint16_t width, int *div)
{
    static const esp_jpeg_image_scale_t scales[] = { JPEG_IMAGE_SCALE_0, JPEG_IMAGE_SCALE_1_2,
                                                     JPEG_IMAGE_SCALE_1_4, JPEG_IMAGE_SCALE_1_8 };
    for (int i = 0; i < 4; i++) {
        if ((width >> i) <= LV_TARGET_WIDTH || i == 3) {
            *div = 1 << i;
            return scales[i];
        }
    }
    *div = 8;
    return JPEG_IMAGE_SCALE_1_8;
}

esp_err_t lv_hsv_analyze(const uint8_t *jpeg, size_t len, lv_hsv_result_t *out)
{
    if (!jpeg || !len || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    int64_t t0 = esp_timer_get_time();

    esp_jpeg_image_cfg_t cfg = {
        .indata = (uint8_t *)jpeg,
        .indata_size = len,
        .out_format = JPEG_IMAGE_FORMAT_RGB888,
    };
    esp_jpeg_image_output_t info;
    esp_err_t err = esp_jpeg_get_image_info(&cfg, &info);
    if (err != ESP_OK) {
        return err;
    }
    int div;
    cfg.out_scale = pick_scale(info.width, &div);
    size_t w = (info.width + div - 1) / div, h = (info.height + div - 1) / div;
    size_t buf_len = w * h * 3;
    uint8_t *rgb = heap_caps_malloc(buf_len, MALLOC_CAP_SPIRAM);
    if (!rgb) {
        return ESP_ERR_NO_MEM;
    }
    cfg.outbuf = rgb;
    cfg.outbuf_size = buf_len;
    cfg.priv.read = 0;

    esp_jpeg_image_output_t img;
    err = esp_jpeg_decode(&cfg, &img);
    if (err != ESP_OK) {
        heap_caps_free(rgb);
        ESP_LOGW(TAG, "No se pudo decodificar el JPEG: %s", esp_err_to_name(err));
        return err;
    }

    uint32_t n = (uint32_t)img.width * img.height, green = 0, yb = 0, dark = 0;
    uint64_t v_sum = 0;
    static uint8_t lut[3][256];  /* solo la usa analysis_task */
    build_levels(rgb, n, lut);
    const uint8_t *p = rgb;
    for (uint32_t i = 0; i < n; i++, p += 3) {
        /* Brillo y oscuridad sobre la imagen original: estirada, una escena a oscuras
         * parecería iluminada. El color, sobre la corregida. */
        uint8_t v = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);
        v_sum += v;
        if (v < V_DARK) {
            dark++;
            continue;
        }
        uint8_t v_adj;
        switch (classify(lut[0][p[0]], lut[1][p[1]], lut[2][p[2]], &v_adj)) {
        case PX_GREEN: green++; break;
        case PX_YELLOW_BROWN: yb++; break;
        default: break;  /* oscuro tras estirar = sombra: no cuenta como color */
        }
    }
    heap_caps_free(rgb);

    out->width = img.width;
    out->height = img.height;
    if (n) {
        out->green_ratio = (float)green / n;
        out->dark_ratio = (float)dark / n;
        out->brightness = (float)v_sum / n / 255.0f;
        float plant = (float)(green + yb) / n;
        out->low_plant_coverage = plant < LV_MIN_PLANT_RATIO;
        out->yellow_brown_ratio = (green + yb) ? (float)yb / (green + yb) : 0.0f;
    }
    out->dark_scene = out->brightness < LV_DARK_BRIGHTNESS;
    /* A oscuras o sin planta a la vista el color no dice nada: no hay sospecha por color. */
    out->suspicion_score = (out->dark_scene || out->low_plant_coverage) ? 0.0f : out->yellow_brown_ratio;
    out->elapsed_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    return ESP_OK;
}
