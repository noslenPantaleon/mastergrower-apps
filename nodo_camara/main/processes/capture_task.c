/**
 * @file capture_task.c
 */
#include "capture_task.h"

#include <inttypes.h>
#include "app_state.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mg_time.h"
#include "net_task.h"
#include "pipeline.h"
#include "system_config.h"

static const char *TAG = "capture_task";

#define CAPTURE_RETRIES     2
#define CAPTURE_TIMEOUT_MS  3000

static esp_err_t capture_with_retry(hal_frame_t *f)
{
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < CAPTURE_RETRIES; i++) {
        err = hal_camera_capture(f, pdMS_TO_TICKS(CAPTURE_TIMEOUT_MS));
        if (err == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Intento %d fallido: %s", i + 1, esp_err_to_name(err));
    }
    return err;
}

static void fail(const trigger_t *t, const char *motivo, esp_err_t err)
{
    ESP_LOGE(TAG, "Captura %s fallida: %s (%s)", mg_trigger_type_str(t->type), motivo, esp_err_to_name(err));
    net_task_publish_event(MG_EVT_ERROR_CAMARA, MG_SEV_MEDIA, NULL, motivo);
    if (t->cmd_id[0]) {
        net_task_send_ack(t->cmd_id, MG_ACK_REJECTED, motivo);
    }
}

static void capture_task(void *arg)
{
    trigger_t t;
    for (;;) {
        xQueueReceive(q_trigger, &t, portMAX_DELAY);
        int64_t latency_ms = (esp_timer_get_time() - t.requested_us) / 1000;

        hal_camera_info_t info;
        hal_camera_get_info(&info);
        if (info.resolution != t.resolucion) {
            esp_err_t err = hal_camera_set_resolution(t.resolucion);
            if (err != ESP_OK) {
                fail(&t, "cambio_resolucion", err);
                continue;
            }
            /* Tras cambiar de resolución los primeros cuadros pueden venir de la anterior. */
            hal_camera_discard_frames(2);
        }

        hal_frame_t frame;
        esp_err_t err = capture_with_retry(&frame);
        if (err != ESP_OK) {
            fail(&t, "error_camara", err);
            continue;
        }

        capture_job_t *job = pipeline_job_new(&t, &frame);
        hal_camera_release(&frame);  // el framebuffer vuelve a la cámara cuanto antes
        if (!job) {
            fail(&t, "sin_memoria", ESP_ERR_NO_MEM);
            continue;
        }
        job->ts = mg_time_is_valid() ? time(NULL) : 0;

        ESP_LOGI(TAG, "captura #%" PRIu32 " %s %ux%u %u bytes (latencia %" PRId64 " ms)", job->seq,
                 mg_trigger_type_str(t.type), job->width, job->height, (unsigned)job->jpeg_len, latency_ms);
        if (latency_ms > 5000) {
            ESP_LOGW(TAG, "Latencia > 5 s: no cumple SRS §3.3");
        }

        if (job->ts) {
            app_state_set_ultima_captura(job->ts);
        }
        if (t.cmd_id[0]) {
            net_task_send_ack(t.cmd_id, MG_ACK_DONE, NULL);
        }

        if (xQueueSend(q_frame, &job, pdMS_TO_TICKS(500)) != pdTRUE) {
            /* C4: la captura ya estará en SD antes de este punto, así que no se pierde. */
            ESP_LOGW(TAG, "q_frame llena: captura #%" PRIu32 " descartada", job->seq);
            pipeline_job_free(job);
        }
    }
}

esp_err_t capture_task_start(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(capture_task, "capture_task", SYS_CAPTURE_STACK, NULL,
                                            SYS_CAPTURE_PRIO, NULL, SYS_CORE_APP);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
