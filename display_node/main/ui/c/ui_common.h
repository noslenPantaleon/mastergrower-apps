/**
 * @file ui_common.h
 * @brief Textos y criterios compartidos por las pantallas (en español, con tildes).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "state_store.h"

/** @brief "14:32" en hora local (CONFIG_MG_UTC_OFFSET_MIN). */
void ui_hhmm(time_t t, char *buf, size_t len);

/** @brief "22/11 14:32" en hora local. */
void ui_ddmm_hhmm(time_t t, char *buf, size_t len);

/** @brief ISO-8601 (contrato) → time_t; 0 si no se puede. */
time_t ui_parse_iso(const char *iso);

/** @brief "Programada", "Manual", ... (trigger_type del contrato). */
const char *ui_trigger_title(const char *trigger_type);

/** @brief "hace 3 min", "hace 2 h"... */
void ui_age(int64_t seconds, char *buf, size_t len);

/** @brief En línea: operativo/autónomo/arrancando y sin dato antiguo. */
bool ui_node_online(const ss_node_t *n);

/** @brief Texto y colores del chip de estado de un nodo ("En línea", "Sin conexión", ...). */
void ui_node_chip(const ss_node_t *n, const char **text, uint32_t *bg, uint32_t *fg);

/** @brief "Luz apagada antes de tiempo", ... (Contrato §3.3). */
const char *ui_event_title(const char *event_type);

/** @brief "alta" / "media" / "baja" y sus colores. */
void ui_severity_chip(const char *severity, const char **text, uint32_t *bg, uint32_t *fg);

/** @brief Alarmas activas: nodos caídos o con error + eventos de severidad alta de las últimas 24 h. */
int ui_active_alarms(void);
