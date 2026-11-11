/**
 * @file analysis_task.c
 */
#include "analysis_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pipeline.h"
#include "system_config.h"

static void analysis_task(void *arg)
{
    capture_job_t *job;
    for (;;) {
        xQueueReceive(q_frame, &job, portMAX_DELAY);
        job->analizado = false;  // C7: local_vision_hsv(job)
        pipeline_job_done(job);
    }
}

esp_err_t analysis_task_start(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(analysis_task, "analysis_task", SYS_ANALYSIS_STACK, NULL,
                                            SYS_ANALYSIS_PRIO, NULL, SYS_CORE_APP);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
