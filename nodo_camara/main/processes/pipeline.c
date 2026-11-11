/**
 * @file pipeline.c
 */
#include "pipeline.h"

#include <inttypes.h>
#include <math.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

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

void pipeline_job_done(capture_job_t *job)
{
    ESP_LOGI(TAG, "captura #%" PRIu32 " fin de pipeline (%s, %ux%u, %u bytes) — C4 la guardará en SD",
             job->seq, mg_trigger_type_str(job->trigger.type), job->width, job->height,
             (unsigned)job->jpeg_len);
    pipeline_job_free(job);
}
