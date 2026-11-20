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
#include "upload_task.h"

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
    job->sensors = (sensor_snapshot_t){ NAN, NAN, NAN };  // C9: sensor de luz / datos del control
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
    cJSON_AddStringToObject(o, "resolution", hal_camera_res_name(job->trigger.resolution));
    cJSON_AddNumberToObject(o, "width", job->width);
    cJSON_AddNumberToObject(o, "height", job->height);
    cJSON_AddStringToObject(o, "priority", job->trigger.priority == PRIORITY_HIGH ? "high" : "normal");
    cJSON_AddNumberToObject(o, "seq", job->seq);

    cJSON *sens = cJSON_AddObjectToObject(o, "sensor_snapshot");
    add_float_or_null(sens, "temperature", job->sensors.temperature);
    add_float_or_null(sens, "humidity", job->sensors.humidity);
    add_float_or_null(sens, "lux", job->sensors.lux);

    cJSON_AddNullToObject(o, "local_analysis");  // C7
    return mg_msg_to_string(o);
}

void pipeline_job_done(capture_job_t *job)
{
    if (job->sq_id[0]) {
        /* C7: antes de esto, storage_queue_update_meta(job->sq_id, ...) con el análisis local. */
        storage_queue_set_ready(job->sq_id);
        ESP_LOGD(TAG, "captura #%" PRIu32 " lista para subir desde la SD (%s)", job->seq, job->sq_id);
    }
    upload_task_submit(job);
}
