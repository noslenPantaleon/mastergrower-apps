/**
 * @file capture_task.c
 */
#include "capture_task.h"

#include <inttypes.h>
#include <stdlib.h>
#include "app_state.h"
#include "hal_storage.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mg_time.h"
#include "net_task.h"
#include "pipeline.h"
#include "system_config.h"

static const char *TAG = "capture_task";

#define CAPTURE_RETRIES     2
#define CAPTURE_TIMEOUT_MS  3000
#define SIZE_RETRIES        3
#define SETTLE_FROM_STREAM_MS 200

static volatile uint8_t s_quality_wanted;  /**< 0 = sin pedido */
static SemaphoreHandle_t s_cam_lock;

bool capture_task_camera_lock(TickType_t timeout)
{
    return s_cam_lock && xSemaphoreTake(s_cam_lock, timeout) == pdTRUE;
}

void capture_task_camera_unlock(void)
{
    xSemaphoreGive(s_cam_lock);
}

void capture_task_set_quality(uint8_t jpeg_quality)
{
    s_quality_wanted = jpeg_quality;
}

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

/** Captura y verifica que el cuadro tenga el tamaño de la resolución pedida (si no, otro intento). */
static esp_err_t capture_expected_size(hal_frame_t *f, hal_camera_res_t res)
{
    uint16_t w, h;
    hal_camera_res_size(res, &w, &h);
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < SIZE_RETRIES; i++) {
        err = capture_with_retry(f);
        if (err != ESP_OK || (f->width == w && f->height == h)) {
            return err;
        }
        ESP_LOGW(TAG, "Cuadro de %ux%u en vez de %ux%u: se descarta", f->width, f->height, w, h);
        hal_camera_release(f);
        err = ESP_ERR_INVALID_SIZE;
    }
    return err;
}

static void fail(const trigger_t *t, const char *reason, esp_err_t err)
{
    ESP_LOGE(TAG, "Captura %s fallida: %s (%s)", mg_trigger_type_str(t->type), reason, esp_err_to_name(err));
    net_task_publish_event(MG_EVT_CAMERA_ERROR, MG_SEV_MEDIUM, NULL, reason);
    if (t->cmd_id[0]) {
        net_task_send_ack(t->cmd_id, MG_ACK_REJECTED, reason);
    }
}

/** Guarda la captura en la SD (store-and-forward). Sin tarjeta, el job sigue solo en memoria. */
static void store_job(capture_job_t *job)
{
    if (!storage_queue_ready()) {
        return;
    }
    char *meta = pipeline_meta_json(job);
    if (!meta) {
        ESP_LOGE(TAG, "Sin memoria para los metadatos de #%" PRIu32, job->seq);
        return;
    }
    esp_err_t err = storage_queue_put(job->jpeg, job->jpeg_len, meta, job->ts,
                                      job->trigger.priority == PRIORITY_HIGH, job->sq_id);
    free(meta);
    if (err != ESP_OK) {
        job->sq_id[0] = '\0';
        ESP_LOGE(TAG, "No se pudo guardar #%" PRIu32 " en la SD: %s", job->seq, esp_err_to_name(err));
        return;
    }

    int free_pct = hal_storage_free_pct();
    if (storage_queue_check_full_transition(free_pct >= 0 ? 100 - free_pct : -1)) {
        char detail[48];
        snprintf(detail, sizeof(detail), "libre %d%%, pendientes %u", free_pct, (unsigned)storage_queue_count());
        net_task_publish_event(MG_EVT_SD_FULL, MG_SEV_MEDIUM, NULL, detail);
    }
}

static void capture_task(void *arg)
{
    trigger_t t;
    for (;;) {
        xQueueReceive(q_trigger, &t, portMAX_DELAY);
        int64_t latency_ms = (esp_timer_get_time() - t.requested_us) / 1000;

        capture_task_camera_lock(portMAX_DELAY);  // si hay stream, espera a que suelte el cuadro actual
        hal_camera_info_t info;
        hal_camera_get_info(&info);
        uint8_t q = s_quality_wanted;
        if (q && info.jpeg_quality != q) {
            esp_err_t qerr = hal_camera_set_quality(q);
            if (qerr == ESP_OK) {
                ESP_LOGI(TAG, "Calidad JPEG %u → %u", info.jpeg_quality, q);
            } else {
                ESP_LOGW(TAG, "No se pudo cambiar la calidad JPEG: %s", esp_err_to_name(qerr));
            }
        }
        if (info.resolution != t.resolution) {
            esp_err_t err = hal_camera_set_resolution(t.resolution);
            if (err != ESP_OK) {
                capture_task_camera_unlock();
                fail(&t, "resolution_change", err);
                continue;
            }
            /* Tras cambiar de resolución los primeros cuadros pueden venir de la anterior o a
             * medio reconfigurar. Desde las resoluciones del stream (QVGA/VGA) la OV3660 tarda
             * más: sin la espera salían capturas UXGA verdosas y con media imagen en blanco (D5). */
            bool from_stream = info.resolution <= HAL_CAM_RES_VGA;
            if (from_stream) {
                vTaskDelay(pdMS_TO_TICKS(SETTLE_FROM_STREAM_MS));
            }
            hal_camera_discard_frames(from_stream ? 4 : 2);
        }

        hal_frame_t frame;
        esp_err_t err = capture_expected_size(&frame, t.resolution);
        if (err != ESP_OK) {
            capture_task_camera_unlock();
            fail(&t, "camera_error", err);
            continue;
        }

        capture_job_t *job = pipeline_job_new(&t, &frame);
        hal_camera_release(&frame);  // el framebuffer vuelve a la cámara cuanto antes
        capture_task_camera_unlock();
        if (!job) {
            fail(&t, "out_of_memory", ESP_ERR_NO_MEM);
            continue;
        }
        job->ts = mg_time_is_valid() ? time(NULL) : 0;

        ESP_LOGI(TAG, "captura #%" PRIu32 " %s %ux%u %u bytes (latencia %" PRId64 " ms)", job->seq,
                 mg_trigger_type_str(t.type), job->width, job->height, (unsigned)job->jpeg_len, latency_ms);
        if (latency_ms > 5000) {
            ESP_LOGW(TAG, "Latencia > 5 s: no cumple SRS §3.3");
        }

        store_job(job);  // primero a la SD: si algo falla después, la captura no se pierde

        if (job->ts) {
            app_state_set_last_capture(job->ts);
        }
        if (t.cmd_id[0]) {
            net_task_send_ack(t.cmd_id, MG_ACK_DONE, NULL);
        }

        if (xQueueSend(q_frame, &job, pdMS_TO_TICKS(500)) != pdTRUE) {
            /* Se saltea el análisis local, pero la captura igual se sube. */
            ESP_LOGW(TAG, "q_frame llena: captura #%" PRIu32 " sin análisis local", job->seq);
            pipeline_job_done(job);
        }
    }
}

esp_err_t capture_task_start(void)
{
    s_cam_lock = xSemaphoreCreateMutex();
    if (!s_cam_lock) {
        return ESP_ERR_NO_MEM;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(capture_task, "capture_task", SYS_CAPTURE_STACK, NULL,
                                            SYS_CAPTURE_PRIO, NULL, SYS_CORE_APP);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
