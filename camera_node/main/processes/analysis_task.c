/**
 * @file analysis_task.c
 * @brief Análisis local de cada captura (C7, Arquitectura §3.1.6) y eventos derivados.
 *
 * - plant_anomaly: al pasar a sospechoso (suspicion_score >= hsv_suspicion_threshold). Se rearma
 *   cuando el puntaje baja del 80 % del umbral, así una planta que sigue igual no repite eventos.
 * - early_lights_off: escena a oscuras dentro de la ventana de luz (con hora válida). Uno por
 *   período oscuro; se rearma con la primera captura iluminada.
 * Una captura sospechosa o que generó un evento pasa a prioridad alta en la SD (sube primero).
 */
#include "analysis_task.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "config_store.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mg_time.h"
#include "net_task.h"
#include "pipeline.h"
#include "system_config.h"
#include "trigger_manager.h"

static const char *TAG = "analysis";

#define REARM_FACTOR       0.8f   /**< histéresis de plant_anomaly */
#define HIGH_SEVERITY_AT   0.6f   /**< puntaje desde el que la anomalía es severidad alta */

static struct {
    bool anomaly_active;
    bool lights_off_active;
    bool warned_mode;
} s_an;

static void check_events(capture_job_t *job)
{
    const lv_hsv_result_t *h = &job->hsv;
    char detail[128];

    if (job->suspicious && !s_an.anomaly_active) {
        s_an.anomaly_active = true;
        job->flagged = true;
        snprintf(detail, sizeof(detail), "puntaje %.2f (umbral %.2f) · amarillo/marrón %.0f%% · verde %.0f%%",
                 (double)h->suspicion_score, (double)job->threshold, (double)h->yellow_brown_ratio * 100,
                 (double)h->green_ratio * 100);
        net_task_publish_event(MG_EVT_PLANT_ANOMALY,
                               h->suspicion_score >= HIGH_SEVERITY_AT ? MG_SEV_HIGH : MG_SEV_MEDIUM, NULL, detail);
    } else if (s_an.anomaly_active && !h->dark_scene && h->suspicion_score < job->threshold * REARM_FACTOR) {
        s_an.anomaly_active = false;
        ESP_LOGI(TAG, "Color de la planta normalizado (puntaje %.2f)", (double)h->suspicion_score);
    }

    bool in_light_window = mg_time_is_valid() && trigger_manager_light_window_active();
    if (h->dark_scene && in_light_window && !s_an.lights_off_active) {
        s_an.lights_off_active = true;
        job->flagged = true;
        snprintf(detail, sizeof(detail), "brillo %.0f%% dentro de la ventana de luz", (double)h->brightness * 100);
        net_task_publish_event(MG_EVT_EARLY_LIGHTS_OFF, MG_SEV_HIGH, NULL, detail);
    } else if (!h->dark_scene && s_an.lights_off_active) {
        s_an.lights_off_active = false;
        ESP_LOGI(TAG, "Escena iluminada otra vez (brillo %.0f%%)", (double)h->brightness * 100);
    }
}

static void analyze(capture_job_t *job)
{
    node_config_t cfg;
    config_store_get(&cfg);
    strlcpy(job->analysis_mode, cfg.analysis_mode, sizeof(job->analysis_mode));
    job->threshold = cfg.hsv_suspicion_threshold;
    if (strcmp(cfg.analysis_mode, "hsv") != 0 && !s_an.warned_mode) {
        s_an.warned_mode = true;
        ESP_LOGW(TAG, "analysis_mode=%s: el modelo todavía no está disponible, se usa solo HSV",
                 cfg.analysis_mode);
    }

    esp_err_t err = lv_hsv_analyze(job->jpeg, job->jpeg_len, &job->hsv);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "#%" PRIu32 " sin análisis: %s", job->seq, esp_err_to_name(err));
        job->analyzed = false;
        return;
    }
    job->analyzed = true;
    job->suspicious = job->hsv.suspicion_score >= job->threshold;

    const lv_hsv_result_t *h = &job->hsv;
    ESP_LOGI(TAG, "#%" PRIu32 " %ux%u en %" PRIu32 " ms · verde %.0f%% · amarillo/marrón %.0f%% · brillo %.0f%%"
             " · puntaje %.2f%s%s%s", job->seq, h->width, h->height, h->elapsed_ms, (double)h->green_ratio * 100,
             (double)h->yellow_brown_ratio * 100, (double)h->brightness * 100, (double)h->suspicion_score,
             job->suspicious ? " · SOSPECHOSA" : "", h->dark_scene ? " · oscura" : "",
             h->low_plant_coverage ? " · poca planta" : "");
    check_events(job);
}

static void analysis_task(void *arg)
{
    capture_job_t *job;
    for (;;) {
        xQueueReceive(q_frame, &job, portMAX_DELAY);
        analyze(job);
        pipeline_job_done(job);
    }
}

esp_err_t analysis_task_start(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(analysis_task, "analysis_task", SYS_ANALYSIS_STACK, NULL,
                                            SYS_ANALYSIS_PRIO, NULL, SYS_CORE_APP);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
