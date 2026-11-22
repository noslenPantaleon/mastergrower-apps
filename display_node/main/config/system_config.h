/**
 * @file system_config.h
 * @brief Constantes del nodo de pantalla: identidad, tareas y núcleos (Arquitectura de pantalla §3.3).
 */
#pragma once

#include "sdkconfig.h"

#define SYS_NODE_ID          CONFIG_MG_NODE_ID
#define SYS_FIRMWARE_VERSION CONFIG_MG_FIRMWARE_VERSION

/* Red y modelo en el núcleo 0; LVGL (la única tarea que toca la interfaz) en el 1. */
#define SYS_CORE_NET   0
#define SYS_CORE_UI    1

/* 24 KB en PSRAM: con 8 KB se desbordaba al armar el detalle de cámara (pestañas, desplegables,
 * TinyTTF y cJSON en los callbacks de api_jobs) */
#define SYS_LVGL_STACK     (24 * 1024)
#define SYS_LVGL_PRIO      4
#define SYS_NET_TASK_STACK 6144
#define SYS_NET_TASK_PRIO  5
#define SYS_THUMB_STACK    8192   /* esp_http_client + cJSON + decodificación JPEG */
#define SYS_THUMB_PRIO     3      /* debajo de net_task */
#define SYS_LIVE_STACK     8192   /* HTTP de la sesión + decodificación JPEG */
#define SYS_LIVE_PRIO      3
#define SYS_API_JOBS_STACK 8192   /* HTTP + decodificación de imágenes del historial */
#define SYS_API_JOBS_PRIO  3
