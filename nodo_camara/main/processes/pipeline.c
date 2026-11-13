/**
 * @file pipeline.c
 */
#include "pipeline.h"

#include <inttypes.h>
#include <math.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "mg_time.h"
#include "system_config.h"

static const char *TAG = "pipeline";

QueueHandle_t q_trigger;
QueueHandle_t q_frame;

static uint32_t s_seq;

esp_err_t pipeline_init(void)
{
    q_trigger = xQueueCreate(PIPELINE_Q_TRIGGER_LEN, sizeof(trigger_t));
    q_frame = xQueueCreate(PIPELINE_Q_FRAME_LEN, sizeof(capture_job_t *));
    return (q_trigger && q_frame) ? ESP_OK : ESP_ERR_NO_MEM;
}

capture_job_t *pipeline_job_new(const trigger_t *trig, const hal_frame_t *frame)
{
    capture_job_t *job = heap_caps_calloc(1, sizeof(*job), MALLOC_CAP_SPIRAM);
    if (!job) {
        return NULL;
    }
    job->jpeg = heap_caps_malloc(frame->len, MALLOC_CAP_SPIRAM);
    if (!job->jpeg) {
        heap_caps_free(job);
        return NULL;
    }
    memcpy(job->jpeg, frame->data, frame->len);
    job->jpeg_len = frame->len;
    job->width = frame->width;
    job->height = frame->height;
    job->trigger = *trig;
    job->seq = ++s_seq;
    job->sensores = (sensores_snapshot_t){ NAN, NAN, NAN };  // C9: sensor de luz / datos del control
    return job;
}

void pipeline_job_free(capture_job_t *job)
{
    if (job) {
        heap_caps_free(job->jpeg);
        heap_caps_free(job);
    }
}

static void add_float_or_null(cJSON *o, const char *k, float v)
{
    if (isnan(v)) {
        cJSON_AddNullToObject(o, k);
    } else {
        cJSON_AddNumberToObject(o, k, v);
    }
}

char *pipeline_meta_json(const capture_job_t *job)
{
    cJSON *o = mg_msg_new(SYS_NODE_ID);
    if (!o) {
        return NULL;
    }
    if (job->ts) {
        char iso[MG_TIME_ISO_LEN];
        mg_time_to_iso(job->ts, iso, sizeof(iso));
        cJSON_AddStringToObject(o, "timestamp", iso);
    } else {
        cJSON_AddNullToObject(o, "timestamp");  // sin hora: la API usa la de recepción
    }
    cJSON_AddStringToObject(o, "camera_type", hal_camera_type());
    cJSON_AddStringToObject(o, "trigger_type", mg_trigger_type_str(job->trigger.type));
    cJSON_AddStringToObject(o, "resolucion", hal_camera_res_name(job->trigger.resolucion));
    cJSON_AddNumberToObject(o, "ancho", job->width);
    cJSON_AddNumberToObject(o, "alto", job->height);
    cJSON_AddStringToObject(o, "prioridad", job->trigger.prioridad == PRIORIDAD_ALTA ? "alta" : "normal");
    cJSON_AddNumberToObject(o, "seq", job->seq);

    cJSON *sens = cJSON_AddObjectToObject(o, "sensores_snapshot");
    add_float_or_null(sens, "temperatura", job->sensores.temperatura);
    add_float_or_null(sens, "humedad", job->sensores.humedad);
    add_float_or_null(sens, "lux", job->sensores.lux);

    cJSON_AddNullToObject(o, "analisis_local");  // C7
    return mg_msg_to_string(o);
}

void pipeline_job_done(capture_job_t *job)
{
    if (job->sq_id[0]) {
        /* C7: storage_queue_update_meta(job->sq_id, ...) con el análisis local. C5 la sube. */
        ESP_LOGI(TAG, "captura #%" PRIu32 " lista en SD como %s", job->seq, job->sq_id);
    } else {
        /* C5: sin SD se intentará subir directo desde memoria. Por ahora se pierde. */
        ESP_LOGW(TAG, "captura #%" PRIu32 " sin SD: se descarta (%u bytes)", job->seq, (unsigned)job->jpeg_len);
    }
    pipeline_job_free(job);
}
