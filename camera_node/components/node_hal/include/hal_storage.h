/**
 * @file hal_storage.h
 * @brief Almacenamiento local del nodo: microSD por SPI montada con FATFS (Arquitectura §3.1.1).
 *        Si no hay tarjeta, hal_storage_init() devuelve error y el nodo sigue sin buffer local.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Punto de montaje en el VFS. */
#define HAL_STORAGE_ROOT "/sd"

/**
 * @brief Monta la tarjeta. No formatea nunca (para no borrar datos por un error de lectura).
 * @return ESP_OK; ESP_ERR_NOT_FOUND si no hay tarjeta o no responde; otro error si no se pudo montar.
 */
esp_err_t hal_storage_init(void);
esp_err_t hal_storage_deinit(void);
bool hal_storage_is_mounted(void);

/** @brief Bytes totales y libres del sistema de archivos. */
esp_err_t hal_storage_get_usage(uint64_t *total, uint64_t *free_bytes);

/** @brief Porcentaje libre (0–100) o -1 si no hay tarjeta montada. */
int hal_storage_free_pct(void);

#ifdef __cplusplus
}
#endif
