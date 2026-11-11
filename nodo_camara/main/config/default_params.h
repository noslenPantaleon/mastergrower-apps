/**
 * @file default_params.h
 * @brief Valores por defecto de la configuración del nodo (Contrato §7). Son el último
 *        nivel de la resolución override → global → default; la config remota los pisa.
 */
#pragma once

#include "sdkconfig.h"

#define DEF_INTERVALO_CAPTURA_MIN  120
#define DEF_VENTANA_LUZ_INICIO     "06:00"
#define DEF_VENTANA_LUZ_FIN        "00:00"
#define DEF_ANALYSIS_MODE          "hybrid"
#define DEF_RESOLUCION             CONFIG_MG_CAPTURE_RES
#define DEF_CALIDAD_JPEG           CONFIG_MG_CAPTURE_JPEG_QUALITY
#define DEF_UMBRAL_SOSPECHA_HSV    0.35f
#define DEF_STREAM_RES_MAX         "vga"
#define DEF_STREAM_FPS_MAX         10
#define DEF_STREAM_TTL_MAX_S       600
