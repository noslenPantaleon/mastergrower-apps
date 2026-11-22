/**
 * @file api_jobs.h
 * @brief Pedidos HTTP a la API sin bloquear la interfaz: una tarea en el núcleo 0 los ejecuta y
 *        los resultados vuelven a la tarea de LVGL (api_jobs_dispatch, desde ui_bridge), donde
 *        corre el callback de cada pantalla. Así las pantallas pueden tocar objetos gráficos en
 *        el callback sin candados.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    int status;           /**< código HTTP; 0 = sin red */
    char *body;           /**< terminado en '\0' (o NULL); se libera después del callback */
    size_t len;
    uint16_t *pixels;     /**< solo api_get_image: RGB565 en PSRAM; el callback puede quedárselo
                               poniendo el puntero en NULL (si no, se libera) */
    uint16_t w, h;
} api_result_t;

typedef void (*api_cb_t)(api_result_t *r, void *ctx);

esp_err_t api_jobs_start(void);

/** @brief GET con cuerpo JSON (hasta max_body bytes). */
esp_err_t api_get(const char *path, size_t max_body, api_cb_t cb, void *ctx);

/** @brief GET de una imagen JPEG que se entrega decodificada (r->pixels). */
esp_err_t api_get_image(const char *path, api_cb_t cb, void *ctx);

/** @brief POST, PUT, PATCH o DELETE con cuerpo JSON opcional. */
esp_err_t api_send(const char *method, const char *path, const char *json, api_cb_t cb, void *ctx);

/** @brief Corre los callbacks de los pedidos terminados. Solo desde la tarea de LVGL. */
void api_jobs_dispatch(void);
