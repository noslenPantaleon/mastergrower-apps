/**
 * @file mg_time.h
 * @brief Sincronización de hora por SNTP y marcas de tiempo ISO-8601 en UTC (Contrato §3.2).
 *        El sistema trabaja siempre en UTC (TZ=UTC0); la hora local es cosa de la interfaz.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(MG_TIME_EVENT);

typedef enum {
    MG_TIME_EVENT_SYNCED,  /**< se sincronizó (la primera vez y en cada resincronización) */
} mg_time_event_id_t;

/** Largo de "2026-11-09T18:30:00Z" + '\0'. */
#define MG_TIME_ISO_LEN 21

/**
 * @brief Inicia SNTP (requiere esp_netif inicializado). No bloquea; lwIP reintenta solo.
 * @param server  NULL → servidores de Kconfig (hasta 3); si no, solo ese.
 */
esp_err_t mg_time_start(const char *server);

/** @brief Espera la primera sincronización. ESP_ERR_TIMEOUT si vence. */
esp_err_t mg_time_wait_sync(TickType_t timeout);

/** @brief true si el reloj tiene una fecha válida (sincronizado al menos una vez o vía RTC). */
bool mg_time_is_valid(void);

/** @brief Hora actual en ISO-8601 UTC ("2026-11-09T18:30:00Z"). buf >= MG_TIME_ISO_LEN. */
void mg_time_now_iso(char *buf, size_t len);

/** @brief Convierte un time_t a ISO-8601 UTC. */
void mg_time_to_iso(time_t t, char *buf, size_t len);

/**
 * @brief Interpreta "YYYY-MM-DDTHH:MM:SS[.fff]Z" (también acepta "+00:00").
 * @return ESP_OK o ESP_ERR_INVALID_ARG si el formato no es válido.
 */
esp_err_t mg_time_parse_iso(const char *iso, time_t *out);

#ifdef __cplusplus
}
#endif
