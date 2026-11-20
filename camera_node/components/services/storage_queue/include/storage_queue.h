/**
 * @file storage_queue.h
 * @brief Cola persistente de capturas pendientes de subir (store-and-forward, Arquitectura §3.1.4).
 *
 * Cada captura son dos archivos en <root>/mg/pend/: <id>.jpg y <id>.json (metadatos del contrato).
 * La escritura es atómica (temporales + rename); al iniciar se reconstruye el índice y se borran
 * restos incompletos. Orden de salida: primero prioridad alta, después la más antigua.
 * Todas las funciones son seguras desde varias tareas.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SQ_ID_MAX_LEN 40

typedef struct {
    char id[SQ_ID_MAX_LEN];
    time_t ts;        /**< 0 = sin hora válida */
    bool high;        /**< prioridad alta: se sube primero y se borra último */
    uint32_t jpeg_len;
    bool in_progress;  /**< todavía en el pipeline (análisis): peek no la entrega hasta set_ready */
} sq_entry_t;

/** @brief Crea la carpeta si hace falta y reconstruye el índice desde la tarjeta. */
esp_err_t storage_queue_init(const char *root);
bool storage_queue_ready(void);

/**
 * @brief Guarda una captura. Antes aplica la política de limpieza si hace falta.
 * @param out_id  recibe el id asignado (SQ_ID_MAX_LEN)
 */
esp_err_t storage_queue_put(const uint8_t *jpeg, size_t jpeg_len, const char *meta_json, time_t ts,
                            bool high, char *out_id);

/**
 * @brief Marca la captura como lista para subir (el pipeline terminó con ella).
 *        Las que se reconstruyen al arrancar ya están listas.
 */
esp_err_t storage_queue_set_ready(const char *id);

/** @brief Reemplaza el .json de una captura (p. ej. para agregar el análisis local). */
esp_err_t storage_queue_update_meta(const char *id, const char *meta_json);

/** @brief Próxima a subir (prioridad alta y más antigua, sin las in_progress). ESP_ERR_NOT_FOUND si no hay. */
esp_err_t storage_queue_peek(sq_entry_t *out);

/**
 * @brief Lee una captura. *jpeg y *meta se reservan en PSRAM: liberar con free().
 *        meta puede ser NULL si no interesa.
 */
esp_err_t storage_queue_read(const char *id, uint8_t **jpeg, size_t *jpeg_len, char **meta);

/** @brief Borra una captura (tras el acuse de la API). */
esp_err_t storage_queue_remove(const char *id);

size_t storage_queue_count(void);

/** @brief Copia hasta max entradas, las más recientes primero. Devuelve cuántas copió. */
size_t storage_queue_list_recent(sq_entry_t *out, size_t max);

/**
 * @brief true SOLO en la transición a "llena" (uso >= CONFIG_MG_SQ_FULL_EVENT_USED_PCT), para
 *        publicar el evento sd_full una vez. Se rearma cuando el uso baja 5 puntos.
 * @param used_pct  porcentaje de uso actual de la tarjeta (100 - libre)
 */
bool storage_queue_check_full_transition(int used_pct);

#ifdef __cplusplus
}
#endif
