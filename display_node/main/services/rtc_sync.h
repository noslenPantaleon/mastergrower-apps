/**
 * @file rtc_sync.h
 * @brief Hora sin red (plan D6): al arrancar toma la hora del RTC con batería, si es válida, así la
 *        interfaz muestra la hora y se pueden mandar comandos (exigen ts válido) antes de tener NTP.
 *        Cada sincronización NTP vuelve a ajustar el RTC.
 */
#pragma once

#include "esp_err.h"

/** @brief Requiere hal_board_init() y el event loop por defecto. Llamar antes de iniciar la red. */
esp_err_t rtc_sync_start(void);
