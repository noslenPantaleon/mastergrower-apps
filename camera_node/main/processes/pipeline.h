/**
 * @file pipeline.h
 * @brief Tipos y colas compartidos por las etapas del pipeline del nodo de cámara
 *        (Arquitectura §3.1.2): trigger_manager → capture_task → analysis_task → (storage/upload).
 *
 * Las colas transportan PUNTEROS a capture_job_t, nunca copias. Quien saca un job de una cola
 * pasa a ser su dueño: o lo envía a la etapa siguiente o lo libera con pipeline_job_free().
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "hal_camera.h"
#include "local_vision.h"
#include "mg_contract.h"
#include "storage_queue.h"

typedef enum {
    PRIORITY_NORMAL = 0,
    PRIORITY_HIGH = 1,  /**< evento de sensor, post-actuación, comando remoto: resolución completa */
} priority_t;

/** Pedido de captura que entra al trigger_manager. */
typedef struct {
    mg_trigger_type_t type;
    char cmd_id[MG_CMD_ID_MAX_LEN + 1]; /**< si vino de un comando: para responder el ack "done" */
} trigger_req_t;

/** Disparo ya validado, listo para capturar (q_trigger). */
typedef struct {
    mg_trigger_type_t type;
    priority_t priority;
    hal_camera_res_t resolution;
    char cmd_id[MG_CMD_ID_MAX_LEN + 1];
    int64_t requested_us; /**< para medir la latencia disparo → captura (requisito < 5 s) */
} trigger_t;

/** Snapshot de sensores ambientales (NAN = sin dato → null en el contrato). */
typedef struct {
    float temperature;
    float humidity;
    float lux;
} sensor_snapshot_t;

/** Una captura recorriendo el pipeline (q_frame, q_result). */
typedef struct {
    uint32_t seq;                /**< número local de captura desde el arranque */
    trigger_t trigger;
    time_t ts;                   /**< hora UTC de la captura (0 si el reloj no era válido) */
    uint8_t *jpeg;               /**< copia propia en PSRAM */
    size_t jpeg_len;
    uint16_t width;
    uint16_t height;
    sensor_snapshot_t sensors;
    char sq_id[SQ_ID_MAX_LEN];   /**< id en la SD ("" si no se pudo guardar o no hay tarjeta) */
    /* Análisis local (C7). analyzed = false si se salteó (q_frame llena o JPEG inválido). */
    bool analyzed;
    lv_hsv_result_t hsv;
    char analysis_mode[8];       /**< modo pedido por la config (hsv | hybrid | model) */
    float threshold;             /**< hsv_suspicion_threshold vigente */
    bool suspicious;             /**< suspicion_score >= threshold → subida prioritaria */
    bool flagged;                /**< generó un evento: también se sube primero */
} capture_job_t;

/** Profundidad de colas: valores iniciales (Arquitectura §5.2: 2 a 4 elementos). */
#define PIPELINE_Q_TRIGGER_LEN 4
#define PIPELINE_Q_FRAME_LEN   3

/** Colas globales del pipeline (creadas por pipeline_init). */
extern QueueHandle_t q_trigger; /**< trigger_t (por valor: es chico) */
extern QueueHandle_t q_frame;   /**< capture_job_t* */

esp_err_t pipeline_init(void);

/** @brief Crea un job copiando el JPEG del framebuffer a PSRAM. NULL si no hay memoria. */
capture_job_t *pipeline_job_new(const trigger_t *trig, const hal_frame_t *frame);
void pipeline_job_free(capture_job_t *job);

/**
 * @brief Metadatos de la captura según el Contrato §5.2 (POST /captures): node_id, timestamp,
 *        camera_type, trigger_type, resolution, priority, sensor_snapshot, local_analysis.
 * @return JSON con malloc (liberar con free) o NULL.
 */
char *pipeline_meta_json(const capture_job_t *job);

/**
 * @brief Fin del recorrido de un job por el pipeline: la marca lista en la SD y la pasa a la
 *        subida (upload_task). Toma posesión del job.
 */
void pipeline_job_done(capture_job_t *job);
