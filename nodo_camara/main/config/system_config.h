/**
 * @file system_config.h
 * @brief Constantes del sistema (no configurables en runtime): identidad, tareas y núcleos.
 */
#pragma once

#include "sdkconfig.h"

#define SYS_NODE_ID          CONFIG_MG_NODE_ID
#define SYS_FIRMWARE_VERSION CONFIG_MG_FIRMWARE_VERSION

/* Núcleos (Arquitectura §3.2): red en el 0 junto a la pila WiFi; captura y análisis en el 1. */
#define SYS_CORE_NET   0
#define SYS_CORE_APP   1

/* Pilas y prioridades iniciales: ajustar midiendo con uxTaskGetStackHighWaterMark. */
#define SYS_NET_TASK_STACK     6144
#define SYS_NET_TASK_PRIO      5
#define SYS_DEBUG_HTTP_STACK   8192
#define SYS_DEBUG_HTTP_PRIO    5
