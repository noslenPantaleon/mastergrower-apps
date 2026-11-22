/**
 * @file ui_thumbs.h
 * @brief Miniaturas del lado de la interfaz (solo desde la tarea de LVGL). Un descriptor de
 *        imagen por nodo con dirección fija: las pantallas lo usan como fuente y, cuando llega
 *        una miniatura nueva, solo cambia el búfer de píxeles.
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"
#include "thumb_service.h"

/** @brief Toma las miniaturas nuevas del thumb_service (llamar ante UI_DIRTY_THUMB). */
void ui_thumbs_sync(void);

/** @brief Descriptor del nodo (NULL si todavía no hay miniatura) y datos de su captura. */
const lv_image_dsc_t *ui_thumb(const char *node_id, thumb_info_t *info);

/** @brief Imagen que cubre a su padre (recorta para llenar), oculta hasta tener fuente. */
lv_obj_t *ui_thumb_image(lv_obj_t *parent);

/** @brief Asigna la miniatura del nodo a la imagen (o la oculta si no hay). */
void ui_thumb_bind(lv_obj_t *img, const char *node_id);

/** @brief Chip del resultado del análisis local de la captura. */
void ui_analysis_chip(const thumb_info_t *info, const char **text, uint32_t *bg, uint32_t *fg);
