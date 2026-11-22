/**
 * @file thumb_service.h
 * @brief Miniaturas de la última captura de cada cámara (SRS de pantalla F-01/F-04, plan D3).
 *
 * Cuando cambia last_capture de una cámara (status por MQTT) pide a la API la última captura
 * (GET /captures?node_id=&limit=1) y, si es nueva, su miniatura (GET /captures/{id}/thumb,
 * JPEG 320×240). La decodifica a RGB565 en PSRAM y la deja "pendiente": la interfaz la toma
 * desde la tarea de LVGL con thumb_service_take() (la red nunca toca objetos gráficos).
 * Si la subida de la cámara viene atrasada, reintenta.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"
#include "state_store.h"

typedef struct {
    char capture_id[24];
    time_t ts;                 /**< timestamp de la captura */
    char trigger_type[20];
    bool has_analysis;
    bool suspicious;
    bool dark_scene;
    bool low_plant;
    float score;               /**< suspicion_score */
} thumb_info_t;

typedef struct {
    uint16_t *pixels;          /**< RGB565, propiedad de quien la toma (liberar con heap_caps_free) */
    uint16_t w, h;
    thumb_info_t info;
} thumb_t;

esp_err_t thumb_service_start(void);

/**
 * @brief Toma la miniatura nueva de un nodo, si hay. Devuelve false si no hay nada pendiente.
 *        La propiedad de out->pixels pasa a quien llama.
 */
bool thumb_service_take(const char *node_id, thumb_t *out);
