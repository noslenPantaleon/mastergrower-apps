/**
 * @file debug_http.h
 * @brief Servidor HTTP de depuración (solo con CONFIG_MG_DEBUG_HTTP). SIN autenticación.
 *
 *   GET /             página con vista previa y controles
 *   GET /capture.jpg  captura JPEG (encabezados X-Capture-Ms, X-Frame-Size)
 *   GET /status       JSON con cámara, memoria y red
 *   GET /control?res=SVGA&quality=12&vflip=1&hmirror=0
 *   GET /storage      JSON: estado de la microSD y últimas capturas pendientes
 *   GET /storage/img?id=<id>  JPEG guardado en la SD
 */
#pragma once

#include "esp_err.h"

esp_err_t debug_http_start(void);
void debug_http_stop(void);
