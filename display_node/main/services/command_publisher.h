/**
 * @file command_publisher.h
 * @brief Comandos de la pantalla hacia los nodos (Contrato §4.3) y seguimiento de su acuse.
 *
 * Cada comando lleva un cmd_id propio ("disp-01-<n>-<azar>"). Los acks llegan por
 * growlab/{tipo}/{id}/cmd/ack (net_task los reenvía acá). Si no llega "accepted" en 5 s o el
 * resultado final en 20 s, el comando queda en CMD_TIMEOUT. Cada cambio se avisa al state_store
 * (SS_CHANGE_CMD) para que la interfaz se actualice.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    CMD_IDLE,       /**< nada pendiente */
    CMD_SENT,       /**< publicado, esperando "accepted" */
    CMD_ACCEPTED,   /**< el nodo lo aceptó, esperando "done" */
    CMD_DONE,
    CMD_REJECTED,   /**< ver reason */
    CMD_TIMEOUT,
} cmd_state_t;

typedef struct {
    cmd_state_t state;
    char reason[32];
    int64_t updated_us;  /**< esp_timer del último cambio */
} cmd_status_t;

esp_err_t command_publisher_init(void);

/**
 * @brief Publica capture_now al nodo de cámara.
 * @return ESP_OK; ESP_ERR_INVALID_STATE si ya hay un comando en curso para ese nodo o no hay
 *         hora válida (el nodo descarta comandos con ts fuera de la ventana de 60 s);
 *         ESP_FAIL si no hay conexión con el broker.
 */
esp_err_t command_capture_now(const char *node_id);

/** @brief Estado del último comando enviado al nodo (CMD_IDLE si nunca se envió). */
cmd_status_t command_status(const char *node_id);

/** @brief Ack recibido (lo llama net_task). */
void command_publisher_on_ack(const char *node_id, const char *json, size_t len);

/** @brief Vencimientos; llamar cada ~1 s. */
void command_publisher_tick(void);
