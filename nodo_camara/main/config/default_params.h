/**
 * @file default_params.h
 * @brief Valores por defecto de la configuración del nodo (Contrato §7). Son el último
 *        nivel de la resolución override → global → default; la config remota los pisa.
 */
#pragma once

#include "sdkconfig.h"

#define DEF_INTERVALO_CAPTURA_MIN  CONFIG_MG_CAPTURE_INTERVAL_MIN
#define DEF_VENTANA_LUZ_INICIO     CONFIG_MG_LIGHT_WINDOW_START
#define DEF_VENTANA_LUZ_FIN        CONFIG_MG_LIGHT_WINDOW_END
#define DEF_UTC_OFFSET_MIN         CONFIG_MG_UTC_OFFSET_MIN
#define DEF_MIN_INTERVALO_DISPARO_S CONFIG_MG_TRIGGER_MIN_INTERVAL_S
#define DEF_RESOLUCION_COMPLETA    CONFIG_MG_CAPTURE_RES_FULL
#define DEF_ANALYSIS_MODE          "hybrid"
#define DEF_RESOLUCION             CONFIG_MG_CAPTURE_RES
#define DEF_CALIDAD_JPEG           CONFIG_MG_CAPTURE_JPEG_QUALITY
#define DEF_UMBRAL_SOSPECHA_HSV    0.35f
#define DEF_STREAM_RES_MAX         "vga"
#define DEF_STREAM_FPS_MAX         10
#define DEF_STREAM_TTL_MAX_S       600
