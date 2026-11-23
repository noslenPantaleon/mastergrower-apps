/**
 * @file hal_rtc.h
 * @brief RTC PCF85063 con batería (I2C 0x51, bus compartido de hal_board). Guarda la hora en UTC.
 */
#pragma once

#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Agrega el RTC al bus I2C. Requiere hal_board_init(). */
esp_err_t hal_rtc_init(void);

/**
 * @brief Lee la hora (UTC).
 * @return ESP_OK; ESP_ERR_INVALID_STATE si el oscilador se detuvo (sin batería o nunca se ajustó).
 */
esp_err_t hal_rtc_get(time_t *out);

/** @brief Ajusta la hora (UTC) y limpia la marca de oscilador detenido. */
esp_err_t hal_rtc_set(time_t t);

#ifdef __cplusplus
}
#endif
