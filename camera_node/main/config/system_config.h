/**
 * @file system_config.h
 * @brief Constantes del sistema (no configurables en runtime): identidad, tareas y núcleos.
 */
#pragma once

#include "esp_app_desc.h"
#include "sdkconfig.h"

#define SYS_NODE_ID          CONFIG_MG_NODE_ID
/* Versión = version.txt del proyecto (PROJECT_VER): la misma que lee la API del .bin para OTA */
#define SYS_FIRMWARE_VERSION (esp_app_get_description()->version)

/* Núcleos (Arquitectura §3.2): red en el 0 junto a la pila WiFi; captura y análisis en el 1. */
#define SYS_CORE_NET   0
#define SYS_CORE_APP   1

/* Pilas y prioridades iniciales: ajustar midiendo con uxTaskGetStackHighWaterMark. */
/* Pipeline (núcleo 1): captura alta, disparos y análisis media. */
#define SYS_CAPTURE_STACK      4096
#define SYS_CAPTURE_PRIO       6
#define SYS_TRIGGER_STACK      3584
#define SYS_TRIGGER_PRIO       5
#define SYS_ANALYSIS_STACK     8192   /* C7: decodificación JPEG + HSV */
#define SYS_ANALYSIS_PRIO      4
#define SYS_STREAM_STACK       6144   /* C8: cámara + esp_websocket_client_send_bin */
#define SYS_STREAM_PRIO        5      /* debajo de captura: un capture_now pasa primero */

#define SYS_NET_TASK_STACK     6144
#define SYS_NET_TASK_PRIO      5
#define SYS_UPLOAD_STACK       6144   /* esp_http_client + cJSON de la respuesta */
#define SYS_UPLOAD_PRIO        4      /* debajo de net_task: MQTT (comandos, LWT) primero */
#define SYS_CONFIG_SYNC_STACK  6144   /* esp_http_client + cJSON de la config */
#define SYS_CONFIG_SYNC_PRIO   3      /* debajo de upload: la config cambia poco */
#define SYS_DEBUG_HTTP_STACK   8192
#define SYS_DEBUG_HTTP_PRIO    5
