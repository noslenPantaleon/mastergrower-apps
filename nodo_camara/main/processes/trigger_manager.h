/**
 * @file trigger_manager.h
 * @brief Único punto de entrada para pedir capturas (Arquitectura §3.1.3).
 *
 * Reglas:
 *  - timer: cada intervalo_captura_min y solo dentro de la ventana de luz (hora local).
 *  - evento_sensor / post_actuacion / comando_remoto: prioridad alta y resolución completa.
 *  - anti-duplicado: se rechaza cualquier disparo a menos de min_intervalo_s del anterior.
 *  - sin hora válida no se puede evaluar la ventana: el timer captura igual (autonomía).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "hal_camera.h"
#include "pipeline.h"

typedef struct {
    uint32_t intervalo_captura_min;
    uint16_t luz_inicio_min;   /**< minutos desde medianoche local (06:00 → 360) */
    uint16_t luz_fin_min;      /**< fin exclusivo; si fin <= inicio la ventana cruza medianoche */
    int16_t utc_offset_min;    /**< hora local = UTC + offset (Argentina: -180) */
    hal_camera_res_t res_rutina;
    hal_camera_res_t res_completa;
    uint16_t min_intervalo_s;
} trigger_config_t;

/** @brief Config inicial a partir de default_params.h (luego la pisa la config remota, C6). */
void trigger_config_default(trigger_config_t *cfg);

esp_err_t trigger_manager_start(const trigger_config_t *cfg);

/** @brief Reemplaza la configuración y reinicia el conteo del timer. */
esp_err_t trigger_manager_set_config(const trigger_config_t *cfg);

/**
 * @brief Pide una captura. Seguro desde cualquier tarea; no bloquea.
 * @param motivo  si no es NULL y el pedido se rechaza, apunta a un texto para el ack
 *                ("fuera_de_ventana_luz", "intervalo_minimo", "cola_llena", ...)
 * @return ESP_OK si quedó encolado.
 */
esp_err_t trigger_request(const trigger_req_t *req, const char **motivo);

/** @brief true si ahora es horario de luz (o si no hay hora válida para saberlo). */
bool trigger_manager_light_window_active(void);

/** @brief "HH:MM" → minutos desde medianoche. false si el formato es inválido. */
bool trigger_parse_hhmm(const char *s, uint16_t *out_min);
