/**
 * @file local_vision.h
 * @brief Análisis local liviano (Arquitectura §3.1.6, etapa 1: HSV).
 *
 * Decodifica el JPEG a una imagen reducida (~200 px de ancho, escala 1/2–1/8 en el propio
 * decodificador) y clasifica cada píxel por matiz, saturación y valor:
 *  - verde (planta sana), amarillo/marrón (hoja decolorada o seca), oscuro.
 * Con eso calcula cobertura de verde, proporción de amarillo/marrón dentro de la planta, brillo
 * medio y si la escena está a oscuras (luz apagada). El puntaje de sospecha es la proporción de
 * amarillo/marrón dentro de la planta (0 si casi no hay planta a la vista).
 *
 * Los umbrales son conservadores y se calibran con imágenes propias (ver docs/plan-desarrollo.md).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Por debajo de esta fracción de píxeles de planta la imagen no permite juzgar color. */
#define LV_MIN_PLANT_RATIO   0.05f
/** Brillo medio (0–1) por debajo del cual la escena se considera a oscuras. */
#define LV_DARK_BRIGHTNESS   0.10f
/** Ancho objetivo de la imagen reducida (la escala se elige para no superarlo). */
#define LV_TARGET_WIDTH      200

typedef struct {
    float green_ratio;          /**< píxeles verdes / total */
    float yellow_brown_ratio;   /**< amarillo+marrón / (verde+amarillo+marrón) */
    float brightness;           /**< V medio, 0–1 */
    float dark_ratio;           /**< píxeles oscuros / total */
    bool dark_scene;            /**< escena a oscuras (luz apagada) */
    bool low_plant_coverage;    /**< menos de LV_MIN_PLANT_RATIO de planta: color no evaluable */
    float suspicion_score;      /**< 0–1 */
    uint16_t width, height;     /**< tamaño analizado */
    uint32_t elapsed_ms;        /**< decodificación + análisis */
} lv_hsv_result_t;

/**
 * @brief Analiza un JPEG. Reserva y libera un buffer temporal en PSRAM (≤ 200×150×3 bytes).
 * @return ESP_OK, ESP_ERR_NO_MEM, o el error del decodificador si el JPEG es inválido.
 */
esp_err_t lv_hsv_analyze(const uint8_t *jpeg, size_t len, lv_hsv_result_t *out);

#ifdef __cplusplus
}
#endif
