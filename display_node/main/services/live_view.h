/**
 * @file live_view.h
 * @brief Vista en vivo, lado espectador (SRS de pantalla F-05, Contrato §5.6, plan D5).
 *
 * Una tarea en el núcleo 0 pide la sesión (POST /stream/sessions), abre el WebSocket de
 * espectador y decodifica los JPEG a RGB565. Siempre se muestra el cuadro más reciente:
 *  - red: si llega un JPEG nuevo antes de decodificar el anterior, el anterior se descarta;
 *  - pantalla: 3 framebuffers (en pantalla, listo, decodificando); si la interfaz no tomó el
 *    listo, el siguiente lo pisa.
 * La interfaz toma los cuadros con live_view_take_frame() desde la tarea de LVGL.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    LIVE_IDLE,
    LIVE_REQUESTING,   /**< pidiendo la sesión a la API */
    LIVE_CONNECTING,   /**< abriendo el WebSocket */
    LIVE_WAITING,      /**< conectado, esperando el primer cuadro de la cámara */
    LIVE_STREAMING,
    LIVE_ENDED,        /**< terminó (msg explica por qué) */
    LIVE_ERROR,
} live_state_t;

typedef struct {
    live_state_t state;
    char msg[72];          /**< texto para la interfaz (en español) */
    char node_id[16];
    char resolution[6];    /**< efectiva (qvga | vga) */
    int fps_max;           /**< efectivo */
    float fps_rx;          /**< cuadros recibidos por segundo */
    uint32_t frames;       /**< recibidos */
    uint32_t dropped;      /**< descartados por atraso (red o decodificación) */
    uint32_t first_frame_ms;  /**< desde el pedido hasta el primer cuadro (0 = todavía no) */
} live_status_t;

esp_err_t live_view_init(void);

/** @brief Pide una sesión (asíncrono). Si había otra abierta la cierra antes. */
esp_err_t live_view_start(const char *node_id, const char *resolution, int fps_max);

/** @brief Cierra la sesión (asíncrono): WebSocket y DELETE /stream/sessions/{id}. */
void live_view_stop(void);

live_status_t live_view_status(void);

/**
 * @brief Toma el cuadro listo más nuevo. Devuelve false si no hay uno nuevo. El cuadro queda
 *        "en pantalla" hasta la próxima llamada que devuelva true (ahí se recicla el anterior).
 */
bool live_view_take_frame(const uint16_t **pixels, uint16_t *w, uint16_t *h);

/** @brief La interfaz dejó de mostrar cuadros (salió de la pantalla): recicla el que tenía. */
void live_view_release_frames(void);
